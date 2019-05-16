open Lwt;
open CalendarLib;

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

let createScheduleIndex = {|
  CREATE UNIQUE INDEX IF NOT EXISTS schedule_time_of_day
    ON schedule (time_of_day);
|}

let createTriggerIndex = {|
  CREATE INDEX IF NOT EXISTS trigger_ts ON trigger (ts);
|}

let stripQuery = (query: string) : string =>
  Str.global_replace(Str.regexp("\n"), "", query);

let makeExecQuery = (query: string) =>
  Caqti_request.exec(Caqti_type.unit, stripQuery(query))

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
      nextTs
    }
  }

let rec getProjectPathRec = (curPath: Fpath.t) : Fpath.t => {
  if (Fpath.basename(curPath) == projectFolder || Fpath.is_root(curPath)) {
    curPath;
  } else {
    getProjectPathRec(Fpath.parent(curPath));
  }
}

let getProjectPath = () : Fpath.t => Sys.argv[0]
  |> Fpath.v
  |> Fpath.parent
  |> getProjectPathRec

let getDBFilePath = () : string => {
  let projectPath = getProjectPath();
  "schedule.db"
    |> Fpath.add_seg(projectPath)
    |> Fpath.to_string
}

let genDBConnection = () : Lwt.t(Caqti_lwt.connection) => {
  let uri = Uri.of_string("sqlite3://" ++ getDBFilePath());
  let%lwt conn = Caqti_lwt.connect(uri) >>= Caqti_lwt.or_fail;
  let (module C) = conn;
  let%lwt _ = C.exec(makeExecQuery(createLogTable), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createScheduleTable), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createTriggerTable), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createScheduleIndex), ())
    >>= Caqti_lwt.or_fail
    >>= () => C.exec(makeExecQuery(createTriggerIndex), ())
    >>= Caqti_lwt.or_fail;
  return(conn);
}