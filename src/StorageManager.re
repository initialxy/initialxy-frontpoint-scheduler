open Lwt;
open CalendarLib;
open FrontpointUnofficialAPI;

type schedulerEvent =
  | TriggerAction
  | ActionSuccess
  | ActionFail
  | Error

let schedulerEventToStr = (event) => switch(event) {
  | TriggerAction => "TriggerAction"
  | ActionSuccess => "ActionSuccess"
  | ActionFail => "ActionFail"
  | Error => "Error"
}

type schedule = {
  id: int64,
  timeOfDay: string,
  nextRunTs: int64,
  action: armState,
}

let projectFolder = "initialxy-frontpoint-scheduler";
let maxLogRows = 10000;

let createLogTable = {|
  CREATE TABLE IF NOT EXISTS log (
    id INTEGER NOT NULL PRIMARY KEY,
    ts INTEGER NOT NULL,
    ref_ts INTEGER NOT NULL,
    event TEXT NOT NULL,
    message TEXT NOT NULL
  );
|};

let createScheduleTable = {|
  CREATE TABLE IF NOT EXISTS schedule (
    id INTEGER NOT NULL PRIMARY KEY,
    time_of_day TEXT NOT NULL,
    next_run_ts INTEGER NOT NULL,
    action TEXT NOT NULL
  );
|};

let createLogTsIndex = {|
  CREATE INDEX IF NOT EXISTS log_ts
    ON log (ts);
|};

let createScheduleTimeOfDayIndex = {|
  CREATE UNIQUE INDEX IF NOT EXISTS schedule_time_of_day
    ON schedule (time_of_day);
|};

let createScheduleNextRunTsIndex = {|
  CREATE INDEX IF NOT EXISTS schedule_next_run_ts
    ON schedule (next_run_ts);
|};

let stripQuery = (query: string): string => query
  |> Str.global_replace(Str.regexp("\n"), "")
  |> Str.global_replace(Str.regexp({|\(^ +\| +$\)|}), "")
  |> Str.global_replace(Str.regexp(" +"), " ");

let makeExecQuery = (query: string) =>
  Caqti_request.exec(Caqti_type.unit, stripQuery(query));

let getNextTimeOfDay = (
  refTs: float,
  hourOfDay: int,
  minuteOfDay: int,
): float => {
  let t = Unix.gmtime(refTs);
  let (nextTs, _) = Unix.mktime(
    {...t, tm_hour: hourOfDay, tm_min: minuteOfDay, tm_sec: 0},
  );
  if (nextTs > refTs) {
    nextTs;
  } else {
    let c = Calendar.from_unixtm(t);
    let c = Calendar.add(c, Calendar.Period.day(1));
    let t = Calendar.to_unixtm(c);
    let (nextTs, _) = Unix.mktime(
      {...t, tm_hour: hourOfDay, tm_min: minuteOfDay, tm_sec: 0},
    );
    nextTs;
  }
}

let rec getProjectPathRec = (curPath: Fpath.t): Fpath.t => {
  if (Fpath.basename(curPath) == projectFolder || Fpath.is_root(curPath)) {
    curPath;
  } else {
    getProjectPathRec(Fpath.parent(curPath));
  }
}

let getProjectPath = () : Fpath.t => Sys.argv[0]
  |> Fpath.v
  |> Fpath.parent
  |> getProjectPathRec;

let getDBFilePath = () : string => "schedule.db"
  |> Fpath.add_seg(getProjectPath())
  |> Fpath.to_string;

let genDBConnection = (): Lwt.t(Caqti_lwt.connection) => {
  let uri = Uri.of_string("sqlite3://" ++ getDBFilePath());
  let%lwt conn = Caqti_lwt.connect(uri) >>= Caqti_lwt.or_fail;
  let (module C) = conn;
  return(conn);
}

let genInitTables = (): Lwt.t(unit) => {
  let%lwt (module C) = genDBConnection();
  let%lwt _ = C.exec(makeExecQuery(createLogTable), ())
    >>= Caqti_lwt.or_fail;
  let%lwt _ = C.exec(makeExecQuery(createScheduleTable), ())
    >>= Caqti_lwt.or_fail;
  let%lwt _ = C.exec(makeExecQuery(createScheduleTimeOfDayIndex), ())
    >>= Caqti_lwt.or_fail;
  let%lwt _ = C.exec(makeExecQuery(createScheduleNextRunTsIndex), ())
    >>= Caqti_lwt.or_fail;
  C.disconnect();
}

let genLog = (
  refTs: float,
  event: schedulerEvent,
  message: string,
): Lwt.t(unit) => {
  let timeStr = Printer.Calendar.sprint(
    "%Y-%m-%dT%H:%M:%S",
    Calendar.convert(
      Calendar.from_unixfloat(refTs),
      Time_Zone.UTC,
      Time_Zone.Local,
    ),
  );
  Console.log(Printf.sprintf(
    "%s: %s - %s",
    timeStr,
    schedulerEventToStr(event),
    message,
  ));
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.exec(
    Caqti_type.(tup4(int64, int64, string, string)),
    "INSERT INTO log (ts, ref_ts, event, message) VALUES (?, ?, ?, ?);",
  );
  let cleanupQuery = Caqti_request.exec(
    Caqti_type.int,
    stripQuery({|
      WITH c AS (SELECT *, ROW_NUMBER() OVER (ORDER BY ts DESC) AS rn FROM log)
      DELETE FROM log WHERE id IN (SELECT id FROM c WHERE rn > ?);
    |}),
  );
  let%lwt _ = C.exec(query, (
    Int64.of_float(Unix.time()),
    Int64.of_float(refTs),
    schedulerEventToStr(event),
    message,
  )) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.exec(cleanupQuery, maxLogRows) >>= Caqti_lwt.or_fail;
  C.disconnect();
}

let getTimeOfDayFromStr = (timeStr: string): (int, int) => {
  if (Str.string_match(
    Str.regexp({|^\([0-9][0-9]\)\([0-9][0-9]\)$|}), timeStr, 0),
  ) {
    (
      int_of_string(Str.matched_group(1, timeStr)),
      int_of_string(Str.matched_group(2, timeStr)),
    );
  } else {
    raise(Failure("Given time string, " ++ timeStr ++ ", is in wrong format"));
  }
}

let genUpdateSchedulesNextRunTime = (
  refTs: float,
  ids: list(int64),
): Lwt.t(unit) => {
  let rawSearchQuery = ids
    |> List.map(id => Int64.to_string(id))
    |> String.concat(", ")
    |> Printf.sprintf("SELECT id, time_of_day FROM schedule WHERE id IN (%s);");
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.unit,
    Caqti_type.(tup2(int64, string)),
    stripQuery(rawSearchQuery),
  );
  let%lwt res = C.collect_list(query, ()) >>= Caqti_lwt.or_fail;
  let%lwt _ = List.fold_left(
    (acc, row) => {
      let (id, timeOfDay) = row;
      let (hour, minute) = getTimeOfDayFromStr(timeOfDay);
      let updateQuery = Caqti_request.exec(
        Caqti_type.(tup2(int64, int64)),
        "UPDATE schedule SET next_run_ts = ? WHERE id = ?;",
      );
      acc
        >>= () => C.exec(
          updateQuery,
          (id, Int64.of_float(getNextTimeOfDay(refTs, hour, minute))),
        )
        >>= Caqti_lwt.or_fail;
    },
    return(),
    res,
  );
  C.disconnect();
}

let genSchedules = (): Lwt.t(list(schedule)) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.unit,
    Caqti_type.(tup4(int64, string, int64, string)),
    stripQuery({|
      SELECT id, time_of_day, next_run_ts, action FROM schedule
      ORDER BY time_of_day;
    |}),
  );
  let%lwt res = C.collect_list(query, ()) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.disconnect();
  return(List.map(row => {
    let (id, timeOfDay, nextRunTs, action) = row;
    {
      id: id,
      timeOfDay: timeOfDay,
      nextRunTs: nextRunTs,
      action: strToArmState(action),
    };
  }, res));
}

let genAddSchedule = (refTs: float, newSchedule: (string, armState)) => {
  let (timeOfDay, action) = newSchedule;
  let (hour, minute) = getTimeOfDayFromStr(timeOfDay);
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.exec(
    Caqti_type.(tup3(string, int64, string)),
    "INSERT INTO schedule (time_of_day, next_run_ts, action) VALUES (?, ?, ?);",
  );
  let%lwt _ = C.exec(query, (
    timeOfDay,
    Int64.of_float(getNextTimeOfDay(refTs, hour, minute)),
    armStateToStr(action),
  )) >>= Caqti_lwt.or_fail;
  C.disconnect();
}

let genRemoveSchedule = (id: int64): Lwt.t(unit) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.exec(
    Caqti_type.int64,
    "DELETE FROM schedule WHERE id = ?;",
  );
  let%lwt _ = C.exec(query, id) >>= Caqti_lwt.or_fail;
  C.disconnect();
}