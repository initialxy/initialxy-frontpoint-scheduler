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

type trigger = {
  ts: float,
  scheduleID: int64,
  action: armState,
}

let projectFolder = "initialxy-frontpoint-scheduler";

let createLogTable = {|
  CREATE TABLE IF NOT EXISTS log (
    id INTEGER NOT NULL PRIMARY KEY,
    ts INTEGER NOT NULL,
    event TEXT NOT NULL,
    message TEXT NOT NULL
  );
|};

let createScheduleTable = {|
  CREATE TABLE IF NOT EXISTS schedule (
    id INTEGER NOT NULL PRIMARY KEY,
    time_of_day TEXT NOT NULL,
    action TEXT NOT NULL
  );
|};

let createTriggerTable = {|
  CREATE TABLE IF NOT EXISTS trigger (
    id INTEGER NOT NULL PRIMARY KEY,
    ts INTEGER NOT NULL,
    schedule_id INTEGER NOT NULL,
    action TEXT NOT NULL
  );
|};

let createScheduleTimeOfDayIndex = {|
  CREATE UNIQUE INDEX IF NOT EXISTS schedule_time_of_day
    ON schedule (time_of_day);
|};

let createTriggerTsIndex = {|
  CREATE INDEX IF NOT EXISTS trigger_ts ON trigger (ts);
|};

let createTriggerScheduleIDIndex = {|
  CREATE UNIQUE INDEX IF NOT EXISTS trigger_schedule_id
    ON trigger (schedule_id);
|};

let stripQuery = (query: string): string =>
  Str.global_replace(Str.regexp("\n"), "", query);

let makeExecQuery = (query: string) =>
  Caqti_request.exec(Caqti_type.unit, stripQuery(query));

let getNextTimeOfDay = (refTs: float, hourOfDay: int, minuteOfDay: int)
  : float => {
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

let getDBFilePath = () : string => {
  let projectPath = getProjectPath();
  "schedule.db"
    |> Fpath.add_seg(projectPath)
    |> Fpath.to_string;
}

let genDBConnection = (): Lwt.t(Caqti_lwt.connection) => {
  let uri = Uri.of_string("sqlite3://" ++ getDBFilePath());
  let%lwt conn = Caqti_lwt.connect(uri) >>= Caqti_lwt.or_fail;
  let (module C) = conn;
  return(conn);
}

let genInitTables = (): Lwt.t(unit) => {
  let%lwt (module C) = genDBConnection();
  C.exec(makeExecQuery(createLogTable), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createScheduleTable), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createTriggerTable), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createScheduleTimeOfDayIndex), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createTriggerTsIndex), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createTriggerScheduleIDIndex), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.disconnect();
}

let genLog = (refTs: float, event: schedulerEvent, message: string)
  : Lwt.t(unit) => {
    let%lwt (module C) = genDBConnection();
    let query = Caqti_request.exec(
      Caqti_type.(tup3(int64, string, string)),
      "INSERT INTO log (ts, event, message) VALUES (?, ?, ?);",
    );
    C.exec(query, (Int64.of_float(refTs), schedulerEventToStr(event), message))
      >>= Caqti_lwt.or_fail
      >>= () => C.disconnect();
  }

let genTriggers = (): Lwt.t(list(trigger)) => {
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.unit,
    Caqti_type.(tup3(int64, int64, string)),
    "SELECT ts, schedule_id, action FROM trigger ORDER BY ts;",
  );
  let%lwt res = C.collect_list(query, ()) >>= Caqti_lwt.or_fail;
  let%lwt _ = C.disconnect();
  return(List.map(row => {
    let (ts, id, action) = row;
    {ts: Int64.to_float(ts), scheduleID: id, action: strToArmState(action)};
  }, res));
}

let getTimeOfDayFromStr = (timeStr: string): (int, int) => {
  if (Str.string_match(Str.regexp({|^\(\d\d\)\(\d\d\)$|}), timeStr, 0)) {
    (
      int_of_string(Str.matched_group(1, timeStr)),
      int_of_string(Str.matched_group(1, timeStr)),
    );
  } else {
    raise(Failure("Given time string, " ++ timeStr ++ ", is in wrong format"));
  }
}

let genUpdateTriggers = (refTs: float, ids: list(int64)): Lwt.t(unit) => {
  let rawSearchQuery = ids
    |> List.map(id => Int64.to_string(id))
    |> String.concat(", ")
    |> Printf.sprintf({|
      SELECT schedule.id, schedule.time_of_day, schedule.action
      FROM trigger t join scheduel s ON t.schedule_id = s.id
      WHERE id IN (%s);
    |});
  let%lwt (module C) = genDBConnection();
  let query = Caqti_request.collect(
    Caqti_type.unit,
    Caqti_type.(tup3(int64, string, string)),
    stripQuery(rawSearchQuery),
  );
  let%lwt res = C.collect_list(query, ()) >>= Caqti_lwt.or_fail;
  let newTriggerValues = List.map(
    row => {
      let (id, timeOfDay, action) = row;
      let (hour, minute) = getTimeOfDayFromStr(timeOfDay);
      (Int64.of_float(getNextTimeOfDay(refTs, hour, minute)), id, action);
    },
    res,
  );
  let rawDeleteQuery = ids
    |> List.map(id => Int64.to_string(id))
    |> String.concat(", ")
    |> Printf.sprintf("DELETE FROM trigger WHERE id IN (%s);");
  let query = Caqti_request.exec(Caqti_type.unit, rawDeleteQuery);
  C.exec(query, ()) >>= Caqti_lwt.or_fail >>= () => C.disconnect();
}

let genAddSchedule = () => {}

let genRemoveSchedule = () => {}