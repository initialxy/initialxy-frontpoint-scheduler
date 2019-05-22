open FrontpointUnofficialAPI;
open Lwt;
open StorageManager;
open Unix;

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
    | e => if (retry > 0) {
      let message = Printf.sprintf(
        "%s failed. Retry: %d - %s",
        armStateToStr(action),
        retry,
        Printexc.to_string(e),
      );
      let%lwt _ = genLog(refTs, ActionFail, message);
      let%lwt _ = Lwt_unix.sleep(10.0);
      genActionWithRetry(refTs, userName, password, action, retry - 1);
    } else {
      fail(e);
    }
  }
}

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  try%lwt(genActionWithRetry(Unix.time(), userName, password, ArmStay, 1)) {
    | Failure(msg) => return(Console.log(msg));
    | _ => return(Console.log("Encountered error"));
  }
}

let cmd = () => {
  let usage = Printf.sprintf(
    "%s %s\nArm Frontpoint security system states at scheduled times of a day",
    Project.name,
    Project.version,
  );

  let toAdd = ref("");
  let toRm = ref("");
  let shouldList = ref(false);
  let runInterval = ref(0);

  let _ = Arg.parse(
    [
      (
        "--list",
        Arg.Unit(() => shouldList := true),
        "List all schedules. <hhmm>-<Disarm/ArmStay/ArmAway>",
      ),
      (
        "--add",
        Arg.String(addStr => toAdd := addStr),
        "Add a new schedule. <hhmm>-<Disarm/ArmStay/ArmAway> "
          ++ "eg. To arm away state at 11pm, enter 2300-ArmAway",
      ),
      (
        "--rm",
        Arg.String(rmStr => toRm := rmStr),
        "Remove a schedule by its time. <hhmm>",
      ),
      (
        "--run-interval",
        Arg.Int(interval => runInterval := interval),
        "Run this script at an interval in seconds.",
      ),
    ],
    _ => Console.log(usage),
    usage,
  );
  
  let%lwt _ = genInitTables();
  let%lwt _ = if (shouldList^) {
    let%lwt schedules = genSchedules();
    return(List.iter(
      schedule => Console.log(Printf.sprintf(
        "%s-%s",
        schedule.timeOfDay,
        armStateToStr(schedule.action),
      )),
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
        return(Console.log("Schedule added"));
      } else {
        fail(Failure("Schedule already exists at time: " ++ timeStr));
      }
    } else {
      raise(Failure(addStr ++ " is not a valid format"));
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
    if (existingSchedule != None) {
      let%lwt _ = genRemoveSchedule(rmStr);
      return(Console.log("Schedule removed"));
    } else {
      fail(Failure("No schedule exists at time: " ++ rmStr));
    }
  } else {
    return();
  }

  return();
}

let () = Lwt_main.run(cmd());