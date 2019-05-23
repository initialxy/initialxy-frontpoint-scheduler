open UnofficialFrontpointAPI;
open Lwt;
open StorageManager;
open Unix;

module SI = Set.Make(Int64);

let setEcho = shouldShow => {
  let tios = tcgetattr(stdin);
  flush(Pervasives.stdout);
  tios.c_echo = shouldShow;
  tcsetattr(stdin, TCSANOW, tios);
}

let readPassword = () => {
  setEcho(false);
  let password = read_line();
  setEcho(true);
  password;
}

let genAction = (
  userName: string,
  password: string,
  action: armState,
): Lwt.t(unit) => {
  let%lwt auth = genLogin(userName, password);
  let%lwt partitionID = genSystemID(auth) >>= genPartitionID(auth);
  let%lwt currentState = genCurrentArmState(auth, partitionID);
  let%lwt _ = if (currentState != action) {
    genArm(auth, partitionID, action);
  } else {
    return();
  }
  genLogout(auth);
}

let rec genActionWithRetry = (
  refTs: float,
  userName: string,
  password: string,
  action: armState,
  retry: int,
): Lwt.t(unit) => {
  try%lwt(genAction(userName, password, action)) {
    | e => {
      let message = Printf.sprintf(
        "%s failed. Retry: %d - %s",
        armStateToStr(action),
        retry,
        Printexc.to_string(e),
      );
      let%lwt _ = genLog(refTs, ActionFail, message);
      if (retry > 0) {
        let%lwt _ = Lwt_unix.sleep(10.0);
        genActionWithRetry(refTs, userName, password, action, retry - 1);
      } else {
        fail(e);
      }
    }
  }
}

let genCheckActionStep = (
  refTs: float,
  schedules: list(schedule),
  userName: string,
  password: string,
): Lwt.t(list(schedule)) => {
  let schedulesToAction = List.filter(s => s.nextRunTs <= refTs, schedules);
  let schedulesToAction = List.sort(
    (l, r) => compare(r.nextRunTs, l.nextRunTs),
    schedulesToAction,
  );
  switch (schedulesToAction) {
    | [] => return(schedules)
    | [s, ...x] => {
      let%lwt _ = genLog(refTs, TriggerAction, scheduleToStr(s));
      let%lwt _ = genActionWithRetry(refTs, userName, password, s.action, 1);
      let%lwt _ = genLog(refTs, ActionSuccess, scheduleToStr(s));
      let%lwt _ = genUpdateSchedulesNextRunTime(
        refTs,
        List.map(s => s.id, schedulesToAction),
      );
      let%lwt schedules = genSchedules();
      let schedulesToActionIDs = SI.of_list(
        List.map(s => s.id, schedulesToAction),
      );
      let updatedSchedules = List.filter(
        s => SI.mem(s.id, schedulesToActionIDs),
        schedules,
      );
      let message = String.concat(", ", List.map(
        scheduleToStr,
        updatedSchedules,
      ));
      let%lwt _ = genLog(refTs, Message, "Bumped: " ++ message);
      return(schedules);
    }
  }
}

let rec genLoop = (
  ~cachedSchedules: option(list(schedule))=?,
  userName: string,
  password: string,
  interval: int,
): Lwt.t(unit) => {
  let refTs = Unix.time();
  let%lwt schedules = switch (cachedSchedules) {
    | Some(cs) => return(cs)
    | None => genSchedules()
  }
  let%lwt schedules = try%lwt(
    genCheckActionStep(refTs, schedules, userName, password),
  ) {
    | e => {
      let%lwt _ = genLog(refTs, Error, Printexc.to_string(e));
      return(schedules);
    }
  }

  let waitTime = refTs +. float_of_int(interval) -. Unix.time();
  let%lwt _ = if (waitTime > 0.0) {
    Lwt_unix.sleep(float_of_int(interval));
  } else {
    return();
  }
  genLoop(~cachedSchedules=schedules, userName, password, interval);
}

let main = () => {
  let usage = Printf.sprintf(
    "%s %s\nArm Frontpoint security system states at scheduled times of a day",
    Project.name,
    Project.version,
  );

  let toAdd = ref("");
  let toRm = ref("");
  let shouldList = ref(false);
  let toRunInterval = ref(0);

  Arg.parse(
    [
      (
        "--list",
        Arg.Unit(() => shouldList := true),
        "List all schedules. "
          ++ "<id>-<nextRunTs>-<hhmm>-<Disarm/ArmStay/ArmAway>",
      ),
      (
        "--add",
        Arg.String(addStr => toAdd := addStr),
        "Add a new schedule. <hhmm>-<Disarm/ArmStay/ArmAway> "
          ++ "eg. To arm away at 11:30pm, enter 2330-ArmAway",
      ),
      (
        "--rm",
        Arg.String(rmStr => toRm := rmStr),
        "Remove a schedule by its time of day. <hhmm>",
      ),
      (
        "--run-interval",
        Arg.Int(interval => toRunInterval := interval),
        "Run this script at an interval in seconds to perform scheduled "
          ++ "actions. You will be asked to enter user name and password "
          ++ "upon launch.",
      ),
    ],
    _ => print_endline(usage),
    usage,
  );
  
  let%lwt _ = genInitTables();
  let%lwt _ = if (shouldList^) {
    let%lwt schedules = genSchedules();
    print_endline("id-nextRunTs-timeOfDay-action");
    return(List.iter(
      schedule => print_endline(scheduleToStr(schedule)),
      schedules,
    ));
  } else {
    return();
  }

  let%lwt _ = if (toAdd^ != "") {
    let addStr = toAdd^;
    let formatRegex = Str.regexp({|^\([0-9]+\)-\([a-zA-Z]+\)$|});
    if (Str.string_match(formatRegex, addStr, 0)) {
      let timeStr = Str.matched_group(1, addStr);
      let actionStr = Str.matched_group(2, addStr);
      let%lwt (_, _) = try(return(getTimeOfDayFromStr(timeStr))) {
        | e => fail(e)
      }
      let%lwt existingSchedule = genFindSchedule(timeStr);
      if (existingSchedule == None) {
        let%lwt _ = genAddSchedule(
          Unix.time(),
          (timeStr, strToArmState(actionStr)),
        );
        let%lwt schedule = genFindSchedule(timeStr);
        switch (schedule) {
          | None => fail(Failure(addStr ++ " doesn't seem to be saved"))
          | Some(s) => genLog(
            Unix.time(),
            Message,
            "Schedule added: " ++ scheduleToStr(s),
          )
        }
      } else {
        fail(Failure("Schedule already exists at time: " ++ timeStr));
      }
    } else {
      fail(Failure(addStr ++ " is not a valid format"));
    }
  } else {
    return();
  }

  let%lwt _ = if (toRm^ != "") {
    let rmStr = toRm^;
    let%lwt (_, _) = try(return(getTimeOfDayFromStr(rmStr))) {
      | e => fail(e)
    }
    let%lwt existingSchedule = genFindSchedule(rmStr);
    switch (existingSchedule) {
      | None => fail(Failure("No schedule exists at time: " ++ rmStr))
      | Some(s) => {
        let%lwt _ = genRemoveSchedule(rmStr);
        genLog(
          Unix.time(),
          Message,
          "Schedule removed: " ++ scheduleToStr(s),
        );
      }
    }
  } else {
    return();
  }

  if (toRunInterval^ > 0) {
    let interval = toRunInterval^;
    print_endline("Enter username:");
    let userName = read_line();
    print_endline("Enter password:");
    let password = readPassword();
    let%lwt schedules = genSchedules();
    print_endline("Start loop");
    genLoop(userName, password, interval);
  } else {
    return();
  }
}

let () = Lwt_main.run(main());