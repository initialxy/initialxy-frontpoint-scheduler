open Lwt;

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
  CREATE UNIQUE INDEX schedule_time_of_day ON schedule (time_of_day);
|};

let createTriggerTable = {|
  CREATE TABLE IF NOT EXISTS trigger (
    id INTEGER NOT NULL PRIMARY KEY,
    ts INTEGER NOT NULL,
    schedule_id INTEGER NOT NULL,
    action TEXT NOT NULL
  );
  CREATE INDEX trigger_ts ON trigger (ts);
|};

let test = () => {
  try%lwt({
    let dir = Sys.argv[0]
      |> Fpath.v
      |> Fpath.parent
      |> Fpath.parent
      |> Fpath.parent
      |> Fpath.parent
      |> Fpath.parent
      |> Fpath.parent
      |> Fpath.parent;
    let dbPath = Fpath.add_seg(dir, "schedule.db");
    let uri = Uri.of_string("sqlite3://" ++ Fpath.to_string(dbPath));
    let%lwt conn = Caqti_lwt.connect(uri) >>= Caqti_lwt.or_fail;
    let req = Caqti_request.exec(Caqti_type.unit, createLogTable);
    let (module C) = conn;
    C.exec(req, ()) >>= Caqti_lwt.or_fail
  }) {
    | Failure(msg) => Lwt.return(Console.log(msg));
    | _ => Lwt.return(Console.log("Encountered error"));
  }
}