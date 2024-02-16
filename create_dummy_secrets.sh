#!/usr/bin/env bash
set -euo pipefail

SECRETS_FILE=secrets.yaml

get_keys () {
    for file in *.yaml; do
        if [[ "$file" == "$SECRETS_FILE" ]]; then continue; fi
        echo "$(printf "%s" "${file%.*}" | tr -- - _)_params";
    done
}

if [[ -e "$SECRETS_FILE" ]]; then
    echo "Secrets file exits" 1>&2;
    exit 1;
fi


get_keys | jq -nR '
    [inputs] |
    map({(.): {
        ip_last_octet: 254,
        ap_password: ("a"*8),
        wifi_password: ("a"*8),
        ota_password: ("a"*32),
        api_key: ("a"*44)}
    }) + [{wifi_ssid: "a"}] |
    add' | yq -y >> "$SECRETS_FILE"

