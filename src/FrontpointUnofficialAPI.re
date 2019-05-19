open Cohttp_lwt_unix;
open Cohttp;
open Lwt;
open Unix;
open Yojson;

type authInfo = {
  afg: string,
  cookie: string,
  token: string,
}

type armState =
  | Disarm
  | ArmStay
  | ArmAway

let armStateToStr = (state) => switch(state) {
  | Disarm => "Disarm"
  | ArmStay => "ArmStay"
  | ArmAway => "ArmAway"
}

let strToArmState = (state) => switch(state) {
  | "Disarm" => Disarm 
  | "ArmStay" => ArmStay
  | "ArmAway" => ArmAway
  | _ => raise(Not_found)
}

let intToArmState = (state) => switch(state) {
  | 1 => Disarm
  | 2 => ArmStay
  | 3 => ArmAway
  | _ => raise(Not_found)
}

let userAgent = "initialxy-frontpoint-scheduler";
let loginURL = "https://my.frontpointsecurity.com/login";
let tokenURL = "https://my.frontpointsecurity.com/api/Login/token";
let redirectURL = "https://my.frontpointsecurity.com/api/Account/AdcRedirectUrl";
let identitiesURL = "https://www.alarm.com/web/api/identities";
let systemsURL = "https://www.alarm.com/web/api/systems/systems"
let partitionsURL = "https://www.alarm.com/web/api/devices/partitions/"
let alarmHomeURL = "https://www.alarm.com/web/system/home";
let alarmLogoutURL = "https://www.alarm.com/web/Logout.aspx";
let frontpointLogoutURL = "https://my.frontpointsecurity.com/login?m=logout";
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
  response: (Response.t, Cohttp_lwt.Body.t),
): Lwt.t((string, string)) => {
  let (meta, body) = response;
  let code = Response.status(meta);
  if (code == `OK || code == `Found) {
    let cookie = Header.get_multi(Response.headers(meta), "set-cookie")
      |> String.concat("; ");
    let%lwt bodyText = Cohttp_lwt.Body.to_string(body)
    return((cookie, bodyText));
  } else {
    fail(Failure(
      Printf.sprintf("Failed response: %d", Code.code_of_status(code)),
    ));
  }
}

let genLogin = (userName: string, password: string): Lwt.t(authInfo) => {
  let%lwt (_, body) = Client.post(
    ~headers=Header.of_list([
      ("Content-Type", contentType),
      ("Referer", loginURL),
      ("User-Agent", userAgent),
    ]),
    ~body=Cohttp_lwt.Body.of_string(Printf.sprintf(
      {|{"Username":"%s","Password":"%s","RememberMe":false}|},
      userName,
      password,
    )),
    Uri.of_string(tokenURL),
  ) >>= genPreprocessResponse;
  let token = stripQuotes(body);
  let%lwt (_, body) = Client.post(
    ~headers=Header.of_list([
      ("Content-Type", contentType),
      ("Cookie", "FPTOKEN=" ++ token),
      ("Authorization", "Bearer " ++ token),
      ("Referer", loginURL),
      ("User-Agent", userAgent),
    ]),
    ~body=Cohttp_lwt.Body.of_string(
      Printf.sprintf({|{"Href":"%s"}|}, loginURL),
    ),
    Uri.of_string(redirectURL),
  )
    >>= genPreprocessResponse;
  let dest = stripQuotes(body);
  let%lwt (cookie, body) = Client.get(Uri.of_string(dest))
    >>= genPreprocessResponse;
  if (Str.string_match(Str.regexp({|.*\bafg=\([^;]+\);.*|}), cookie, 0)) {
    let afg = Str.matched_group(1, cookie);
    return({afg: afg, cookie: cookie, token: token});
  } else {
    fail(Failure("afg not found in cookie"));
  }
}

let createGetAuthHeaders = (auth: authInfo) => Header.of_list([
  ("Accept", acceptType),
  ("Referer", alarmHomeURL),
  ("User-Agent", userAgent),
  ("AjaxRequestUniqueKey", auth.afg),
  ("Cookie", auth.cookie),
]);

let createPostAuthHeaders = (auth: authInfo) => Header.add(
  createGetAuthHeaders(auth),
  "Content-Type",
  contentType,
);

let genSystemID = (auth: authInfo): Lwt.t(string) => {
  let%lwt (_, body) = Client.get(
    ~headers=createGetAuthHeaders(auth),
    Uri.of_string(identitiesURL),
  ) >>= genPreprocessResponse;
  try ({
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
    return(switch (systemIDs) {
      | [i, ...x] => i
      | _ => raise(Not_found)
    });
  }) {
    | e => fail(e)
  }
}

let genPartitionID = (auth: authInfo, systemID: string): Lwt.t(string) => {
  let%lwt (_, body) = Client.get(
    ~headers=createGetAuthHeaders(auth),
    Uri.of_string(systemsURL ++ "/" ++ systemID),
  ) >>= genPreprocessResponse;
  try ({
    open Yojson.Basic.Util;
    let paritionIDs = Yojson.Basic.from_string(body)
      |> member("data")
      |> member("relationships")
      |> member("partitions")
      |> member("data")
      |> to_list
      |> List.map(data => data
        |> member("id")
        |> to_string,
      );
    return(switch (paritionIDs) {
      | [i, ...x] => i
      | _ => raise(Not_found)
    });
  }) {
    | e => fail(e)
  }
}

let genCurrentArmState = (
  auth: authInfo,
  partitionID: string,
): Lwt.t(armState) => {
  let%lwt (_, body) = Client.get(
    ~headers=createGetAuthHeaders(auth),
    Uri.of_string(partitionsURL ++ "/" ++ partitionID),
  ) >>= genPreprocessResponse;
  try ({
    open Yojson.Basic.Util;
    let state = Yojson.Basic.from_string(body)
      |> member("data")
      |> member("attributes")
      |> member("state")
      |> to_int
      |> intToArmState
    return(state);
  }) {
    | e => fail(e)
  }
}

let genArm = (
  auth: authInfo,
  partitionID: string,
  state: armState,
): Lwt.t(unit) => {
  let%lwt _ = Client.post(
    ~headers=createPostAuthHeaders(auth),
    Uri.of_string(Printf.sprintf(
      "%s/%s/%s",
      partitionsURL,
      partitionID,
      armStateToStr(state),
    )),
    ~body=Cohttp_lwt.Body.of_string({|{"statePollOnly":false}|}),
  ) >>= genPreprocessResponse;
  return();
}

let genLogout = (auth: authInfo): Lwt.t(unit) => {
  let%lwt _ = Client.get(
    ~headers=createGetAuthHeaders(auth),
    Uri.of_string(alarmLogoutURL),
  ) >>= genPreprocessResponse;
  let%lwt _ = Client.get(
    ~headers=Header.of_list([
      ("Accept", "text/html"),
      ("Cookie", "FPTOKEN=" ++ auth.token),
      ("Referer", alarmHomeURL),
      ("User-Agent", userAgent),
    ]),
    Uri.of_string(frontpointLogoutURL),
  ) >>= genPreprocessResponse;
  return();
}