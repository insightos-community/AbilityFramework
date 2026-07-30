// Copyright (c) 2026 InsightOS (https://insightos.cn)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "discoverymgr/election_mgr.hpp"
#include "uuid.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
using ElectionMsgFactory = std::unique_ptr<
    electionmgr::
        ElectionManager> (*)(std::unique_ptr<electionmgr::ElectionInterface>, uuids::uuid, const nlohmann::json&);

std::unique_ptr<electionmgr::ElectionManager> make_election_mgr_bully(
    uuids::uuid self_id,
    const nlohmann::json& params,
    std::unique_ptr<electionmgr::ElectionInterface> intf
);
