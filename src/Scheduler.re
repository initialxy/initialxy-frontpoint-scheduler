open Lwt;
open Cohttp;
open Cohttp_lwt_unix;
open Sqlite3;
open Unix;
open Yojson;

type authInfo = {
  afg: string,
  cookie: string,
  token: string
}

let userAgent = "initialxy-frontpoint-scheduler";
let loginURL = "https://my.frontpointsecurity.com/login";
let tokenURL = "https://my.frontpointsecurity.com/api/Login/token";
let redirectURL = "https://my.frontpointsecurity.com/api/Account/AdcRedirectUrl";
let identitiesURL = "https://www.alarm.com/web/api/identities";
let alarmHomeURL = "https://www.alarm.com/web/system/home";
let contentType = "application/json";
let acceptType = "application/vnd.api+json";

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

let stripQuotes = quotedStr => {
  if (Str.string_match(Str.regexp({|^"\(.+\)"$|}), quotedStr, 0)) {
    Str.matched_group(1, quotedStr);
  } else {
    quotedStr;
  }
}

let genPreprocessResponse = (
  extra: option('a),
  response: (Response.t, Cohttp_lwt.Body.t),
) : Lwt.t((option('a), string, string)) => {
  let (meta, body) = response;
  let code = Response.status(meta);
  if (code == `OK || code == `Found) {
    let cookie = String.concat(
      "; ",
      Header.get_multi(Response.headers(meta), "set-cookie"),
    );
    Cohttp_lwt.Body.to_string(body)
    >>= bodyText => Lwt.return((extra, cookie, bodyText));
  } else {
    Lwt.fail(Failure("Response failed"));
  }
}

let genLogin = (userName: string, password: string) : Lwt.t(authInfo) => {
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
  >>= genPreprocessResponse(None)
  >>= result => {
    let (_, _, body) = result
    let token = stripQuotes(body);
    Client.post(
      ~headers = Header.of_list([
        ("Content-Type", contentType),
        ("Cookie", "FPTOKEN=" ++ token),
        ("Authorization", "Bearer " ++ token),
        ("Referer", loginURL),
        ("User-Agent", userAgent),
      ]),
      ~body = Cohttp_lwt.Body.of_string(
        Printf.sprintf({|{"Href":"%s"}|}, loginURL),
      ),
      Uri.of_string(redirectURL),
    )
    >>= genPreprocessResponse(Some(token))
  }
  >>= result => {
    let (maybeToken, _, body) = result;
    let dest = stripQuotes(body)
    Client.get(Uri.of_string(dest))
    >>= genPreprocessResponse(maybeToken)
  }
  >>= result => {
    let (maybeToken, cookie, body) = result;
    if (maybeToken != None) {
      let tokenText = switch(maybeToken) {
        | None => ""
        | Some(t) => t
      }
      if (Str.string_match(Str.regexp({|.*\bafg=\([^;]+\);.*|}), cookie, 0)) {
        let afg = Str.matched_group(1, cookie);
        Lwt.return({afg: afg, cookie: cookie, token: tokenText});
      } else {
        Lwt.fail(Failure("afg not found in cookie"));
      }
    } else {
      Lwt.fail(Failure("Token not found"));
    }
  }
}

let genSystemID = (auth: authInfo) => {
  Client.get(
    ~headers = Header.of_list([
      ("Accept", acceptType),
      ("Referer", alarmHomeURL),
      ("User-Agent", userAgent),
      ("AjaxRequestUniqueKey", auth.afg),
      ("Cookie", auth.cookie),
    ]),
    Uri.of_string(identitiesURL),
  )
  >>= genPreprocessResponse(None)
  >>= result => {
    let (_, _, body) = result;
    try ((() => {
      open Yojson.Basic.Util;
      let systemIDs = Yojson.Basic.from_string(body)
        |> member("data")
        |> to_list
        |> List.map(data => data
          |> member("relationships")
          |> member("selectedSystem")
          |> member("data")
          |> member("id")
          |> to_string,
        );
      Lwt.return(switch (systemIDs) {
        | [i, ...x] => i
        | _ => raise(Not_found)
      });
    })()) {
      | _ => Lwt.fail(Not_found)
    }
  }
}

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  Lwt.catch(
    () => {
      genLogin(userName, password)
      >>= genSystemID
      >>= systemID => {
        Console.log(systemID);
        Lwt.return();
      }
    },
    e => {
      Lwt.return(switch(e) {
        | Failure(msg) => Console.log(msg);
        | _ => Console.log("error");
      });
    },
  );
}

type user = {
  id: int,
  name: string,
}

let () = Lwt_main.run(main());