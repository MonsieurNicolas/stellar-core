// Copyright 2019 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "herder/QuorumTracker.h"
#include "scp/LocalNode.h"
#include <Tracy.hpp>
#include <deque>

namespace stellar
{
QuorumTracker::QuorumTracker(NodeID const& localNodeID)
    : mLocalNodeID(localNodeID)
{
}

bool
QuorumTracker::isNodeDefinitelyInQuorum(NodeID const& id)
{
    auto it = mQuorum.find(id);
    return it != mQuorum.end();
}

bool
QuorumTracker::expand(NodeID const& id, SCPQuorumSetPtr qSet)
{
    bool res = false;
    auto it = mQuorum.find(id);
    if (it != mQuorum.end())
    {
        if (it->second == nullptr)
        {
            it->second = qSet;
            LocalNode::forAllNodes(*qSet, [&](NodeID const& id) {
                // inserts an edge node if needed
                mQuorum.insert(std::make_pair(id, nullptr));
            });
            res = true;
        }
        else if (it->second == qSet)
        {
            // nop
            res = true;
        }
    }
    return res;
}

void
QuorumTracker::rebuild(std::function<SCPQuorumSetPtr(NodeID const&)> lookup)
{
    ZoneScoped;
    mQuorum.clear();
    std::deque<NodeID> backlog;
    backlog.push_back(mLocalNodeID);
    while (!backlog.empty())
    {
        auto& n = *backlog.begin();

        // `n` is in quorum
        auto ii = mQuorum.emplace(n, nullptr);

        if (ii.second || ii.first->second == nullptr)
        {
            auto qSet = lookup(n);
            if (qSet != nullptr)
            {
                LocalNode::forAllNodes(
                    *qSet, [&](NodeID const& id) { backlog.emplace_back(id); });
                expand(n, qSet);
            }
        }

        backlog.pop_front();
    }
}

QuorumTracker::QuorumMap const&
QuorumTracker::getQuorum() const
{
    return mQuorum;
}
}
