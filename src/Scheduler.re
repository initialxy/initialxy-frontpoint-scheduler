open Lwt;
open Cohttp;
open Cohttp_lwt_unix;
open Sqlite3;
open Unix;

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

let preprocessResponse = (
  extra: option('a),
  response: (Response.t, Cohttp_lwt.Body.t),
) : Lwt.t((option('a), Code.status_code, string, string)) => {
  let (meta, body) = response;
  let code = Response.status(meta);
  if (code == `OK || code == `Found) {
    let cookie = String.concat("; ",Header.get_multi(Response.headers(meta), "set-cookie"));
    Cohttp_lwt.Body.to_string(body)
    >>= bodyText => Lwt.return((extra, code, cookie, bodyText));
  } else {
    Lwt.fail(Failure("Response failed"));
  }
}

let stripQuotes = quotedStr => {
  if (Str.string_match(Str.regexp({|^"\(.+\)"$|}), quotedStr, 0)) {
    Lwt.return(Str.matched_group(1, quotedStr));
  } else {
    Lwt.fail(Failure("Can't strip quotes"));
  }
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
  >>= preprocessResponse(None)
  >>= result => {
    let (_, _, _, body) = result
    stripQuotes(body)
  }
  >>= token => {
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
    >>= preprocessResponse(Some(token))
  }
  >>= result => {
    let (maybeToken, _, _, body) = result;
    stripQuotes(body)
    >>= dest => {
      Client.get(Uri.of_string(dest))
      >>= preprocessResponse(maybeToken)
    }
  }
  >>= result => {
    let (maybeToken, _, cookie, body) = result;
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

let main = () => {
  Console.log("Enter username:");
  let userName = read_line();
  Console.log("Enter password:");
  let password = readPassword();
  Lwt.catch(
    () => login(userName, password)
    >>= auth => {
      Console.log(auth.afg);
      Console.log(auth.token);
      Console.log(auth.cookie);
      Lwt.return();
    },
    e => {
      Lwt.return(switch(e) {
        | Failure(msg) => Console.log(msg);
        | _ => Console.log("error");
      });
    },
  );
}

let () = Lwt_main.run(main());