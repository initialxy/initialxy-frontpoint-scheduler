**DO NOT USE THIS REPO! This is my personal RaspberryPi project. It is not meant to be secure nor reliable.**

---

# initialxy-frontpoint-scheduler
A script that takes actions (disarm, arm stay, arm away) on Frontpoint Security system at scheduled times. Despite having all that fancy smart app and whatnot, Frontpoint does not offer a way to simply action at scheduled times, nor does it officially offer HomeKit integration. This repo offers a script that uses Frontpoint's web site API to take actions on your home security system. Inspirations were drawn from [node-frontpoint](https://github.com/jhurliman/node-frontpoint), though this repo contains an alternate implementation of unofficial Frontpoint API. Big thanks to [node-frontpoint](https://github.com/jhurliman/node-frontpoint) nethertheless.


# Security Warning
Frontpoint does not offer an official API, so this script simulates their web site traffic, which does not offer proper authentication tokens. Therefore there's no way to make this script as secure as it should be. Every action must be taken with raw user name and password. For this reason, this script is **not** meant to be scheduled by [cron](https://en.wikipedia.org/wiki/Cron), instead, it is simply a long running script that should be started in terminal, and it will ask for user name and password every time upon launch to avoid saving authentication information in plain text on persistent storage.

# Usage
Install [esy](https://github.com/esy/esy) first and initialize with `esy install`. Run with `esy run` on the project root directory.

However on the actual RaspberryPi, it's a bit more difficult, because esy doesn't seem to work on ARM processor even though it installs correctly. But we can still build with [dune](https://github.com/ocaml/dune). Install [opam](https://opam.ocaml.org/) first and swtich to OCaml 4.06.1 (this is the latest version that meets all package min and max version requirements as of writing). Install dune and let dune tell you what packages you need to install from opam. However you must run the executable with **absolute path** (because I didn't want to drag in another heavy library just to get absolute path in OCaml). eg.

    $ sudo apt-get install opam
    $ opam init
    $ opam switch 4.06.1
    $ opam install dune reason
    $ dune external-lib-deps --missing @@default  # Run the opam command it prints out
    $ dune build
    $ /home/your/path/to/initialxy-frontpoint-scheduler/_build/default/src/Scheduler.exe --help

| Option | Description |
| --- | --- |
| `--list` | List all schedules. `<id>-<nextRunTs>-<hhmm>-<Disarm/ArmStay/ArmAway>` |
| `--add` | Add a new schedule. `<hhmm>-<Disarm/ArmStay/ArmAway>` eg. To arm away at 11:30pm, enter `2330-ArmAway` |
| `--rm` | Remove a schedule by its time of day. `<hhmm>` |
| `--min-interval` | Minimum time between actions to avoid overwhelming the API. |

Example:

    $ esy run --list
    id-nextRunTs-timeOfDay-action
    1-1558621800-0730-Disarm
    3-1558558800-1400-ArmAway
    2-1558591200-2300-ArmAway
    $ esy run --add 1500-ArmStay
    2019-05-22T14:59:35: Message - Schedule added: 4-1558562400-1500-ArmStay
    $ esy run --rm 1500
    2019-05-22T14:59:42: Message - Schedule removed: 4-1558562400-1500-ArmStay
    $ esy run --run-interval 10
    Enter username:
    lol
    Enter password:
    Start loop

# Technology
This is my hobby project to play with something new. It is entirely coded in [Reason](https://reasonml.github.io/) native for the sole purpose of doing it just for fun. It is a rather niche language and compiling it for native is an even more niche use case. I write Haskell and JavaScript professionally, so I figured it would be fun to try it out. It experiments with the following technologies:
* [lwt](https://ocsigen.org/lwt/4.1.0/manual/manual)
* [lwt_ppx](https://ocsigen.org/lwt/3.2.1/api/Ppx_lwt)
* [SQLite3](https://github.com/mmottl/sqlite3-ocaml)
* [Caqti](https://github.com/paurkedal/ocaml-caqti)
* [cohttp](https://github.com/mirage/ocaml-cohttp)
* [Yojson](https://github.com/ocaml-community/yojson)

As well as general mix of OCaml syntax with Reason syntax such as Monadic expression, pipe etc. I intended to keep it as pure as possible to the spirit of functional programming, so mutable state is avoided as much as possible.

# License
[MIT](https://opensource.org/licenses/MIT)