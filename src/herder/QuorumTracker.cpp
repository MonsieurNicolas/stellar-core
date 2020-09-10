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
        auto& info = it->second;
        if (info.mQSet == nullptr)
        {
            info.mQSet = qSet;
            int newDist = info.mDistance + 1;
            res = true;
            LocalNode::forAllNodes(*qSet, [&](NodeID const& lid) {
                if (!res)
                {
                    return;
                }

                // inserts an edge node if needed
                auto r = mQuorum.emplace(lid, NodeInfo{nullptr, newDist});
                if (!r.second) // there was already an entry
                {
                    auto& old = r.first->second;
                    if (old.mDistance > newDist)
                    {
                        // it was strictly worst

                        // if `expand` was already called on `old` we need to
                        // rebuild
                        if (old.mQSet)
                        {
                            res = false;
                            return;
                        }
                        else
                        {
                            // otherwise, we can override the entry
                            old.mClosestQSetValidators.clear();
                            old.mDistance = newDist;
                        }
                    }
                    else if (old.mDistance < newDist)
                    {
                        // it was strictly better, no need to update anything
                        return;
                    }
                    // else same distance, we need to merge the closest
                    // validators sets
                }
                auto& cit = r.first;
                if (newDist == 1)
                {
                    // this is a node from qset
                    cit->second.mClosestQSetValidators.emplace(lid);
                }
                else
                {
                    // inherit validators from the parent
                    cit->second.mClosestQSetValidators.insert(
                        info.mClosestQSetValidators.begin(),
                        info.mClosestQSetValidators.end());
                }
            });
        }
        else if (it->second.mQSet == qSet)
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

    // perform a full rebuild by performing a BFS traversal of the quorum
    // centered on the local node using `mQSet` as a marker to track which nodes
    // were already visited

    std::deque<NodeID> backlog;
    backlog.push_back(mLocalNodeID);
    mQuorum.emplace(mLocalNodeID, NodeInfo{nullptr, 0});
    while (!backlog.empty())
    {
        auto& n = *backlog.begin();
        auto it = mQuorum.find(n);
        if (it->second.mQSet == nullptr)
        {
            auto qSet = lookup(n);
            if (qSet != nullptr)
            {
                LocalNode::forAllNodes(
                    *qSet, [&](NodeID const& id) { backlog.emplace_back(id); });
                bool r = expand(n, qSet);
                if (!r)
                {
                    // as we're doing BFS, we should always call "expand" on
                    // nodes that are further and further from the local node
                    throw std::runtime_error(
                        "Invalid state while rebuilding quorum state");
                }
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
