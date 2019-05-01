open Lwt;
open Cohttp;
open Cohttp_lwt_unix;
open Sqlite3;
open Unix;

let userAgent = "initialxy-frontpoint-scheduler";
let loginURL = "https://my.frontpointsecurity.com/login";
let tokenURL = "https://my.frontpointsecurity.com/api/Login/token";
let redirectURL = "https://my.frontpointsecurity.com/api/Account/AdcRedirectUrl";
let identitiesURL = "https://www.alarm.com/web/api/identities";
let contentType = "application/json";

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

let login = (userName: string, password: string) => {
    Client.post(
      ~headers = Header.of_list([
        ("Content-Type", contentType),
        ("Referer", loginURL),
        ("User-Agent", userAgent),
      ]),
      ~body = Cohttp_lwt.Body.of_string(Printf.sprintf(
        {|{"Username":"%s","Password":"%s","RememberMe":false}|},
        userName,
        password
      )),
      Uri.of_string(tokenURL),
    )
    >>= result => {
      let (resp, body) = result;
      let code = Response.status(resp);
      if (code == `OK) {
        Cohttp_lwt.Body.to_string(body)
        >>= token => {
          if (Str.string_match(Str.regexp("\"\\(.+\\)\""), token, 0)) {
            let token = Str.matched_group(1, token);
            Console.log(token);
          }
          Lwt.return();
        }
      } else {
        Lwt.return();
      }
    }
}

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  login(userName, password)
}

let () = Lwt_main.run(main());