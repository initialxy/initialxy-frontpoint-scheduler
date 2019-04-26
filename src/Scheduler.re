open Lwt;
open Cohttp;
open Cohttp_lwt_unix;
open Sqlite3;

let main =
  Client.get(Uri.of_string("https://initialxy.com"))
  >>= result => {
    let (resp, body) = result;
    Cohttp_lwt.Body.to_string(body)
    >>= b => {
      Console.log(b);
      Lwt.return();
    }
  }

let () = Lwt_main.run(main);