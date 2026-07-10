/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "defs.h"
#include "storage/disk_manager.h"
#include "common/config.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>

static constexpr std::chrono::duration<int64_t> FLUSH_TIMEOUT = std::chrono::seconds(3);
// the offset of log_type_ in log header
static constexpr int OFFSET_LOG_TYPE = 0;
// the offset of lsn_ in log header
static constexpr int OFFSET_LSN = sizeof(int);
// the offset of log_tot_len_ in log header
static constexpr int OFFSET_LOG_TOT_LEN = OFFSET_LSN + sizeof(lsn_t);
// the offset of log_tid_ in log header
static constexpr int OFFSET_LOG_TID = OFFSET_LOG_TOT_LEN + sizeof(uint32_t);
// the offset of prev_lsn_ in log header
static constexpr int OFFSET_PREV_LSN = OFFSET_LOG_TID + sizeof(txn_id_t);
// offset of log data
static constexpr int OFFSET_LOG_DATA = OFFSET_PREV_LSN + sizeof(lsn_t);
// sizeof log_header
static constexpr int LOG_HEADER_SIZE = OFFSET_LOG_DATA;

/* P1：WAL 批帧头 —— magic + batch_len + crc32(body)；恢复按批校验，残缺批整批丢弃 */
static constexpr uint32_t WAL_BATCH_MAGIC = 0x574C4247u;  // 'WLBG'
static constexpr int WAL_BATCH_HDR_SIZE = 12;             // u32 + u32 + u32

/* IEEE CRC32（查表，无外部依赖；批仅 KB～MB 级） */
inline uint32_t wal_crc32(const char* data, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        crc = table[(crc ^ (uint8_t)data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

