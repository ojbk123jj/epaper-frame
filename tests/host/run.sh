#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

INC_COMP=components/epd_gdem042f86/include
INC_DISPLAY=components/epaper_display/include
INC_IMAGE=components/image_processing/include
INC_MODELSCOPE=components/modelscope_client/include
INC_MODELSCOPE_PRIVATE=components/modelscope_client/private_include
INC_CONFIG=components/app_config/include

run() {
    local name="$1"; shift
    echo "== $name"
    gcc -std=c11 -Wall -Wextra -Werror -O1 \
        -I "$INC_COMP" \
        -o "/tmp/$name" "$@" && "/tmp/$name"
}

run test_fb_pack tests/host/test_fb_pack.c
run test_frame_transfer -I components/frame_transfer/include tests/host/test_frame_transfer.c components/frame_transfer/src/frame_transfer.c
run test_ble_policy -I components/ble_image/private_include -I components/frame_transfer/include \
    tests/host/test_ble_policy.c components/ble_image/src/radio_lifecycle.c components/ble_image/src/ble_policy.c
run test_frame_service -DAPP_CONFIG_NO_LOCAL -I components/frame_service/include \
    -I "$INC_DISPLAY" -I "$INC_IMAGE" -I "$INC_MODELSCOPE" -I "$INC_CONFIG" \
    -I components/network_service/include -I tests/host/service_stubs -I tests/host/epd_stubs \
    tests/host/test_frame_service.c components/frame_service/src/frame_service.c
run test_frame_service_ai -DAPP_CONFIG_NO_LOCAL '-DAPP_WIFI_SSID="host"' '-DAPP_MODELSCOPE_TOKEN="host"' \
    -I components/frame_service/include -I "$INC_DISPLAY" -I "$INC_IMAGE" \
    -I "$INC_MODELSCOPE" -I "$INC_CONFIG" -I components/network_service/include \
    -I tests/host/service_stubs -I tests/host/epd_stubs \
    tests/host/test_frame_service.c components/frame_service/src/frame_service.c
run test_epd_safety -I tests/host/epd_stubs tests/host/test_epd_safety.c
run test_display -I "$INC_DISPLAY" -I tests/host/display_stubs -I tests/host/epd_stubs tests/host/test_display.c
run test_ai_images -I "$INC_IMAGE" -I tests/host/epd_stubs -I third_party/stb \
    tests/host/test_ai_images.c components/image_processing/src/image_convert.c components/image_processing/src/image_decode.c -lm
run test_ai_http -I "$INC_MODELSCOPE_PRIVATE" -I "$INC_CONFIG" -I tests/host/ai_stubs -I tests/host/epd_stubs tests/host/test_ai_http.c

idf_root="${IDF_PATH:-}"
if [[ -z "$idf_root" && -f build/project_description.json ]]; then
    idf_root=$(python3 -c 'import json; print(json.load(open("build/project_description.json"))["idf_path"])')
fi
if [[ ! -f "$idf_root/components/json/cJSON/cJSON.c" ]]; then
    echo "Set IDF_PATH (source ESP-IDF export.sh) to run protocol tests."
    exit 1
fi
run test_modelscope_protocol -I "$INC_MODELSCOPE_PRIVATE" -I tests/host/epd_stubs -I "$idf_root/components/json/cJSON" \
    tests/host/test_modelscope_protocol.c components/modelscope_client/src/modelscope_protocol.c "$idf_root/components/json/cJSON/cJSON.c" -lm

run test_modelscope_client -DAPP_CONFIG_NO_LOCAL \
    -I "$INC_MODELSCOPE" -I "$INC_MODELSCOPE_PRIVATE" -I "$INC_CONFIG" \
    -I tests/host/epd_stubs -I "$idf_root/components/json/cJSON" \
    tests/host/test_modelscope_client.c components/modelscope_client/src/modelscope_client.c \
    components/modelscope_client/src/modelscope_protocol.c "$idf_root/components/json/cJSON/cJSON.c" -lm
