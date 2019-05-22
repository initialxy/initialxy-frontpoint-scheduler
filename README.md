## DO NOT USE THIS REPO! This project is for my personal RaspberryPi project. Since neither Frontpoint Security nor Alarm.com offer official API, this project uses their web site API, which means it is not meant to be secure nor reliable in a long running script. To quote the MIT license, which this repo uses: THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

---

# initialxy-frontpoint-scheduler
A script that takes actions (disarm, arm stay, arm away) on Frontpoint Security system at scheduled times. Despite having all that fancy smart app and whatnot, Frontpoint does not offer a way to simply schedule actions at scheduled times, nor does it officially offer HomeKit integration. This repo offers a script that uses Frontpoint's web site API to take actions on your home security system. Inspirations were drawn from [node-frontpoint](https://github.com/jhurliman/node-frontpoint), though this repo contains an alternate implementation of Frontpoint web traffic. Big thanks to [node-frontpoint](https://github.com/jhurliman/node-frontpoint) nethertheless.


# Warning
Like the above heading already mentioned, Frontpoint does not offer official API, hence this script simulates their web site traffic. It does not offer proper authentication tokens, therefore there's no way to keep this script as secure as it should be. Every action must be taken with raw user name and password. For this reason, this script is **not** meant to be scheduled by [cron](https://en.wikipedia.org/wiki/Cron), instead, it is simply a long running script that should be started in terminal, and it will ask for user name and password every time upon launch to avoid saving authentication information in plain text on persistent storage. Of course this comes at a cost of reliability, but I consider it a worthy trade off. One might as, why not encrypt it with AES? I shall paraphrase [Pidgin](https://en.wikipedia.org/wiki/Pidgin_(software))'s answer: since this script needs to fetch raw authentication information when taking every action, it needs to be able to decrypt authentication information, therefore it needs to hold onto cypher key along with ciphertext. Doing so effectively means raw information is stored with an extra step. Providing a mere illusion of security. This script choses not to save any of that at all, though authentication information is still kept in memory space, which should be guarded by OS.

# Usage
Run this script with `esy run` on the project root directory.

| Option | Description |
| --- | --- |
| `--list` | List all schedules. `<id>-<nextRunTs>-<hhmm>-<Disarm/ArmStay/ArmAway>` |
| `--add` | Add a new schedule. `<hhmm>-<Disarm/ArmStay/ArmAway>` eg. To arm away at 11:30pm, enter `2330-ArmAway` |
| `--rm` | Remove a schedule by its time of day. `<hhmm>` |
| `--run-interval` | Run this script at an interval in seconds to perform scheduled actions. You will be asked to enter user name and password upon launch. |

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

# License
[MIT](https://opensource.org/licenses/MIT)