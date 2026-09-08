# Copyright (c) 2026 Silicon Laboratories Inc.
# SPDX-License-Identifier: Apache-2.0

set(BLOBS_DIR ${ZEPHYR_CURRENT_MODULE_DIR}/zephyr/blobs/simplicity_sdk)

# Get SoC series number, i.e. translate efr32bg22 -> 22, mgm240p -> 24, simg301 -> 301
if(CONFIG_SOC_SERIES MATCHES "^ef[rm]32")
  string(SUBSTRING ${CONFIG_SOC_SERIES} 7 2 SILABS_DEVICE_FAMILY_NUMBER)
elseif(CONFIG_SOC_SERIES MATCHES "^si")
  string(SUBSTRING ${CONFIG_SOC_SERIES} 4 3 SILABS_DEVICE_FAMILY_NUMBER)
elseif(CONFIG_SOC_SERIES MATCHES "^.gm")
  string(SUBSTRING ${CONFIG_SOC_SERIES} 3 2 SILABS_DEVICE_FAMILY_NUMBER)
else()
  message(FATAL_ERROR "unknown part")
endif()

function(add_prebuilt_library lib_name prebuilt_path)
  if(NOT CONFIG_BUILD_ONLY_NO_BLOBS)
    add_library(${lib_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${lib_name} PROPERTIES
      IMPORTED_LOCATION ${BLOBS_DIR}/${prebuilt_path}
    )
    zephyr_link_libraries(${lib_name})
  endif()
endfunction()

if(CONFIG_OPENTHREAD_LIBRARY)

  zephyr_system_include_directories(${ZEPHYR_OPENTHREAD_MODULE_DIR}/include)
  zephyr_system_include_directories(${ZEPHYR_OPENTHREAD_MODULE_DIR}/examples/platforms)

  if(CONFIG_OPENTHREAD_FTD)
    set(device "ftd")
    set(cli_lib openthread-cli-ftd)
  elseif(CONFIG_OPENTHREAD_MTD)
    set(device "mtd")
    set(cli_lib openthread-cli-mtd)
  else()
    message(FATAL_ERROR "Unkown OpenThread device type")
  endif()

  if(CONFIG_OPENTHREAD_MULTIPLE_INSTANCE)
    set(mi "_mi")
  endif()

  if(CONFIG_OPENTHREAD_COAP)
    set(coap "_coap")
  endif()

  add_prebuilt_library(sl_ot_stack openthread/libsl_ot_stack_${device}${mi}${coap}_efr32mg${SILABS_DEVICE_FAMILY_NUMBER}_gcc.a)
  add_prebuilt_library(sl_ot_tcp openthread/libsl_ot_stack_tcp_efr32mg${SILABS_DEVICE_FAMILY_NUMBER}_gcc.a)

  # CLI

  if(CONFIG_SHELL)
    zephyr_library_named(sl_ot_cli)

    zephyr_library_sources(
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_api.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_ba.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_bbr.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_br.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_coap.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_coap_secure.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_commissioner.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_dataset.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_dns.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_history.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_joiner.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_link_metrics.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_logging.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_mac_filter.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_mdns.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_mesh_diag.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_network_data.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_ping.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_srp_client.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_srp_server.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_tcat.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_tcp.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_udp.cpp
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/cli/cli_utils.cpp
    )

    zephyr_library_compile_definitions(OPENTHREAD_PROJECT_CORE_CONFIG_FILE="openthread-core-zephyr-config.h")
    zephyr_library_compile_definitions(OPENTHREAD_API_VERSION="${CONFIG_OPENTHREAD_API_VERSION}")
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_FTD                    OPENTHREAD_FTD=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_MTD                    OPENTHREAD_MTD=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_LINK_METRICS_INITIATOR OPENTHREAD_CONFIG_MLE_LINK_METRICS_INITIATOR_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_MAC_FILTER             OPENTHREAD_CONFIG_MAC_FILTER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_PING_SENDER            OPENTHREAD_CONFIG_PING_SENDER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_COAPS                  OPENTHREAD_CONFIG_COAP_SECURE_API_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_COAP_BLOCK             OPENTHREAD_CONFIG_COAP_BLOCKWISE_TRANSFER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_BACKBONE_ROUTER        OPENTHREAD_CONFIG_BACKBONE_ROUTER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_BORDER_ROUTER          OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_DATASET_UPDATER        OPENTHREAD_CONFIG_DATASET_UPDATER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_NETDATA_PUBLISHER      OPENTHREAD_CONFIG_NETDATA_PUBLISHER_ENABLE=1)
    zephyr_library_compile_definitions_ifdef(CONFIG_OPENTHREAD_ENABLE_SERVICE         OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE=1)

    zephyr_library_include_directories(
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/core
      ${ZEPHYR_OPENTHREAD_MODULE_DIR}/src/include
    )

    zephyr_library_link_libraries(sl_ot_stack)
  endif()
endif()
