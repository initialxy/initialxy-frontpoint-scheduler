open FrontpointUnofficialAPI;
open Lwt;

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  Lwt.catch(
    () => {
      genLogin(userName, password)
      >>= auth => {
        genSystemID(auth)
        >>= genPartitionID(auth)
        >>= partitionID => {
          genCurrentArmState(auth, partitionID)
          >>= state => {
            Console.log(state);
            genArm(auth, partitionID, ArmStay);
          }
          >>= () => genLogout(auth)
        }
      }
    },
    e => {
      Lwt.return(switch(e) {
        | Failure(msg) => Console.log(msg);
        | _ => Console.log("Encountered error");
      });
    },
  );
}

let () = Lwt_main.run(main());