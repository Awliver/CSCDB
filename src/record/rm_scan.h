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

#include "rm_defs.h"

class RmFileHandle;

class RmScan : public RecScan {
    const RmFileHandle *file_handle_;
    Rid rid_;
    // Hold the current data page while walking its bitmap.  Fetching and
    // unpinning the same page once per record makes large sequential scans
    // spend most of their time in buffer-pool bookkeeping.
    int pinned_page_no_ = -1;
    Page *pinned_page_ = nullptr;
    char *pinned_bitmap_ = nullptr;
    char *pinned_slots_ = nullptr;

    void pin_current_page();
    void release_current_page();
public:
    RmScan(const RmFileHandle *file_handle);

    RmScan(const RmScan &) = delete;
    RmScan &operator=(const RmScan &) = delete;

    ~RmScan() override;

    void next() override;

    bool is_end() const override;

    Rid rid() const override;

    // Valid until next() advances to another page or the scan is destroyed.
    const char *record_data() const;
};
