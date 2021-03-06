open Lwt;
open CalendarLib;
open UnofficialFrontpointAPI;

type schedulerEvent =
  | ActionFail
  | ActionSuccess
  | Error
  | Message
  | TriggerAction

let schedulerEventToStr = (event) => switch(event) {
  | ActionFail => "ActionFail"
  | ActionSuccess => "ActionSuccess"
  | Error => "Error"
  | Message => "Message"
  | TriggerAction => "TriggerAction"
}

type schedule = {
  id: int64,
  timeOfDay: string,
  nextRunTs: float,
  action: armState,
}

let scheduleToStr = (schedule: schedule) => Printf.sprintf(
  "%Ld-%Ld-%s-%s",
  schedule.id,
  Int64.of_float(schedule.nextRunTs),
  schedule.timeOfDay,
  armStateToStr(schedule.action),
)

let dbRowToSchedule = (row: (int64, string, int64, string)): schedule => {
  let (id, timeOfDay, nextRunTs, action) = row;
  {
    id: id,
    timeOfDay: timeOfDay,
    nextRunTs: Int64.to_float(nextRunTs),
    action: strToArmState(action),
  };
}

let maxLogRententionInSec = float_of_int(30 * 24 * 3600);

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
  let t = Unix.localtime(refTs);
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
  if (
    Fpath.basename(curPath) == Project.name ||
    Fpath.is_current_dir(curPath) ||
    Fpath.is_root(curPath)
  ) {
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
  print_endline(Printf.sprintf(
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
    Caqti_type.int64,
    "DELETE FROM log WHERE ts < ?;",
  );
  let%lwt _ = C.exec(query, (
    Int64.of_float(Unix.time()),
    Int64.of_float(refTs),
    schedulerEventToStr(event),
    message,
  )) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.exec(
    cleanupQuery,
    Int64.of_float(Unix.time() -. maxLogRententionInSec),
  ) >>= Caqti_lwt.or_fail;
  C.disconnect();
}

let getTimeOfDayFromStr = (timeStr: string): (int, int) => {
  if (Str.string_match(
    Str.regexp({|^\([0-9][0-9]\)\([0-9][0-9]\)$|}), timeStr, 0),
  ) {
    let hour = int_of_string(Str.matched_group(1, timeStr));
    let minute = int_of_string(Str.matched_group(2, timeStr));
    if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
      (hour, minute);
    } else {
      raise(Failure(
        "Given time string, " ++ timeStr ++ ", is not a valid time",
      ));
    }
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
          (Int64.of_float(getNextTimeOfDay(refTs, hour, minute)), id),
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
  return(List.map(dbRowToSchedule, res));
}

let genFindSchedule = (timeOfDay: string): Lwt.t(option(schedule)) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.string,
    Caqti_type.(tup4(int64, string, int64, string)),
    stripQuery({|
      SELECT id, time_of_day, next_run_ts, action FROM schedule
      WHERE time_of_day = ?;
    |}),
  );
  let%lwt res = C.collect_list(query, timeOfDay) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.disconnect();
  switch (res) {
    | [row] => return(Some(dbRowToSchedule(row)))
    | [] => return(None)
    | _ => fail(Failure("Multiple schedules on the same time"))
  }
}

let genNextSchedule = (): Lwt.t(option(schedule)) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.unit,
    Caqti_type.(tup4(int64, string, int64, string)),
    stripQuery({|
      SELECT id, time_of_day, next_run_ts, action FROM schedule
      ORDER BY next_run_ts LIMIT 1;
    |}),
  );
  let%lwt res = C.collect_list(query, ()) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.disconnect();
  switch (res) {
    | [row] => return(Some(dbRowToSchedule(row)))
    | _ => return(None)
  }
}

let genSchedulesUntilTs = (ts: float): Lwt.t(list(schedule)) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.int64,
    Caqti_type.(tup4(int64, string, int64, string)),
    stripQuery({|
      SELECT id, time_of_day, next_run_ts, action FROM schedule
      WHERE next_run_ts <= ? ORDER BY next_run_ts DESC;
    |}),
  );
  let%lwt res = C.collect_list(query, Int64.of_float(ts)) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.disconnect();
  return(List.map(dbRowToSchedule, res));
}

let genAddSchedule = (
  refTs: float,
  newSchedule: (string, armState),
): Lwt.t(unit)=> {
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

let genRemoveSchedule = (timeOfDay: string): Lwt.t(unit) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.exec(
    Caqti_type.string,
    "DELETE FROM schedule WHERE time_of_day = ?;",
  );
  let%lwt _ = C.exec(query, timeOfDay) >>= Caqti_lwt.or_fail;
  C.disconnect();
}
