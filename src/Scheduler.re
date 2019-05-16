open FrontpointUnofficialAPI;
open Lwt;
open StorageManager;

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  try%lwt({
    let%lwt auth = genLogin(userName, password);
    let%lwt partitionID = genSystemID(auth) >>= genPartitionID(auth);
    let%lwt currentState = genCurrentArmState(auth, partitionID)
    Console.log(currentState);
    genArm(auth, partitionID, ArmStay) >>= () => genLogout(auth);
  }) {
    | Failure(msg) => Lwt.return(Console.log(msg));
    | _ => Lwt.return(Console.log("Encountered error"));
  }
}

let () = Lwt_main.run(main());