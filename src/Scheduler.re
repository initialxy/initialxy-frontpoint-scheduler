open Lwt;
open Cohttp;
open Cohttp_lwt_unix;
open Sqlite3;
open Unix;

type authInfo = {
  cookie: string,
  token: string
}

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

let login = (userName: string, password: string) : Lwt.t(authInfo) => {
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
    } else {
      Lwt.fail(Failure("Token fetch failed"))
    }
  }
  >>= token => {
    if (Str.string_match(Str.regexp({|"\(.+\)"|}), token, 0)) {
      let token = Str.matched_group(1, token);
      Lwt.return({cookie: "", token: token});
    } else {
      Lwt.fail(Failure("Token in unexpected format"))
    }
  }
}

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  Lwt.catch(
    () => login(userName, password)
    >>= auth => {
      Console.log(auth.token);
      Lwt.return();
    },
    e => {
      switch(e) {
        | Failure(msg) => Console.log(msg);
        | _ => Console.log("error");
      }
      Lwt.return();
    },
  );
}

let () = Lwt_main.run(main());