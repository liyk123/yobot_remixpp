#pragma once
#define DEFAULT_CONFIG R"({
    "host": "0.0.0.0",
    "port": 9444,
    "access_token": "",
    "client_salt": null,
    "public_address": null,
    "public_basepath": "/",
    "web_mode_hint": true,
    "super-admin": [],
    "black-list": [],
    "white_list_mode": false,
    "black-list-group": [],
    "white-list-group": [],
    "allow_bulk_private": false,
    "clan_battle_mode": "web",
    "notify_groups": [],
    "notify_privates": [],
    "preffix_on": false,
    "preffix_string": "",
    "zht_in": false,
    "zht_out": false,
    "zht_out_style": "s2t",
    "show_icp": false,
    "icp_info": "",
    "gongan_info": "",
    "web_gzip": 0,

    "boss":{
        "jp": [
            [6000000, 8000000, 10000000, 12000000, 15000000],
            [6000000, 8000000, 10000000, 12000000, 15000000],
            [7000000, 9000000, 13000000, 15000000, 20000000],
            [15000000, 16000000, 18000000, 19000000, 20000000]
        ],
        "cn": [
            [6000000, 8000000, 10000000, 12000000, 20000000],
            [6000000, 8000000, 10000000, 12000000, 20000000],
            [6000000, 8000000, 10000000, 12000000, 20000000]
        ],
        "tw": [
            [6000000, 8000000, 10000000, 12000000, 15000000],
            [6000000, 8000000, 10000000, 12000000, 15000000],
            [6000000, 8000000, 10000000, 12000000, 15000000]
        ]
    },
    "level_by_cycle":{
        "cn":[[1,3],[4,10],[11,999]],
        "jp":[[1,3],[4,10],[11,45],[46,999]],
        "tw":[[1,3],[4,10],[11,999]]
    },
    "boss_id":{
        "cn":["302100","302000","300600","300800","300100"],
        "jp":["302100","302000","300600","300800","300100"],
        "tw":["302100","302000","300600","300800","300100"]
    }
}
)"