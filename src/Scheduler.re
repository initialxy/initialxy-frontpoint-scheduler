open FrontpointUnofficialAPI;
open Lwt;
open StorageManager;
open Unix;

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

let () = Lwt_main.run(main());