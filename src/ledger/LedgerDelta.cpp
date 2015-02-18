#include "ledger/LedgerDelta.h"

namespace stellar
{
    void LedgerDelta::addEntry(EntryFrame const& entry)
    {
        addEntry(entry.copy());
    }

    void LedgerDelta::deleteEntry(EntryFrame const& entry)
    {
        deleteEntry(entry.copy());
    }

    void LedgerDelta::modEntry(EntryFrame const& entry)
    {
        modEntry(entry.copy());
    }

    void LedgerDelta::addEntry(EntryFrame::pointer entry)
    {
        auto k = entry->getKey();
        auto del_it = mDelete.find(k);
        if (del_it != mDelete.end())
        {
            // delete + new is an update
            mDelete.erase(del_it);
            mMod[k] = entry;
        }
        else
        {
            assert(mNew.find(k) == mNew.end()); // double new
            assert(mMod.find(k) == mMod.end()); // mod + new is invalid
            mNew[k] = entry;
        }
    }

    void LedgerDelta::deleteEntry(EntryFrame::pointer entry)
    {
        auto k = entry->getKey();
        deleteEntry(k);
    }

    void LedgerDelta::deleteEntry(LedgerKey const& k)
    {
        auto new_it = mNew.find(k);
        if (new_it != mNew.end())
        {
            // new + delete -> don't add it in the first place
            mNew.erase(new_it);
        }
        else
        {
            assert(mDelete.find(k) == mDelete.end()); // double delete is invalid
            // only keep the delete
            mMod.erase(k);
            mDelete.insert(k);
        }
    }

    void LedgerDelta::modEntry(EntryFrame::pointer entry)
    {
        auto k = entry->getKey();
        auto mod_it = mMod.find(k);
        if ( mod_it != mMod.end())
        {
            // collapse mod
            mod_it->second = entry;
        }
        else
        {
            auto new_it = mNew.find(k);
            if (new_it != mNew.end())
            {
                // new + mod = new (with latest value)
                new_it->second = entry;
            }
            else
            {
                assert(mDelete.find(k) == mDelete.end()); // delete + mod is illegal
                mMod[k] = entry;
            }
        }
    }

    void LedgerDelta::merge(LedgerDelta &other)
    {
        for (auto &d : other.mDelete)
        {
            deleteEntry(d);
        }
        for (auto &n : other.mNew)
        {
            addEntry(n.second);
        }
        for (auto &m : other.mMod)
        {
            modEntry(m.second);
        }
    }

    xdr::msg_ptr LedgerDelta::getTransactionMeta() const
    {
        LedgerDelta::MetaHelper me(*this);
        return xdr::xdr_to_msg(me);
    }

    Constexpr const std::size_t LedgerDelta::MetaHelper::size() const
    {
        return mLedgerDelta.mNew.size() + mLedgerDelta.mMod.size() + mLedgerDelta.mDelete.size();
    }

    void LedgerDelta::MetaHelper::check_size(uint32_t i) const
    {
        abort(); // not implemented
    }

    void LedgerDelta::MetaHelper::resize(uint32_t i)
    {
        abort(); // not implemented
    }

    LedgerDelta::MetaHelper::value_type &LedgerDelta::MetaHelper::extend_at(uint32_t i)
    {
        abort(); // not implemented
        return CLFEntryBase();
    }

    LedgerDelta::MetaHelper::ValueIterator LedgerDelta::MetaHelper::begin() const
    {
        return ValueIterator(*this, true);
    }

    LedgerDelta::MetaHelper::ValueIterator LedgerDelta::MetaHelper::end() const
    {
        return ValueIterator(*this, false);
    }

    LedgerDelta::MetaHelper::ValueIterator::ValueIterator(LedgerDelta::MetaHelper const& me, bool begin)
        : mLedgerDelta(me.mLedgerDelta)
    {
        if (begin)
        {
            mIter = mLedgerDelta.mNew.begin();
            mState = newNodes;
        }
        else
        {
            mState = done;
        }
    }

    LedgerDelta::MetaHelper::ValueIterator& LedgerDelta::MetaHelper::ValueIterator::operator++()
    {
        switch (mState)
        {
        case newNodes:
            if (mIter != mLedgerDelta.mNew.end())
            {
                mIter++;
                if (mIter != mLedgerDelta.mNew.end())
                    return *this;
            }
            mIter = mLedgerDelta.mMod.begin();
            mState = modNodes;
        case modNodes:
            if (mIter != mLedgerDelta.mMod.end())
            {
                mIter++;
                if (mIter != mLedgerDelta.mMod.end())
                    return *this;
            }
            mDelIter = mLedgerDelta.mDelete.begin();
            mState = delNodes;
        case delNodes:
            if (mDelIter != mLedgerDelta.mDelete.end())
            {
                mDelIter++;
                if (mDelIter != mLedgerDelta.mDelete.end())
                    return *this;
            }
            mState = done;
        default:
            return *this;
        }
    }

    bool LedgerDelta::MetaHelper::ValueIterator::operator==(const LedgerDelta::MetaHelper::ValueIterator& rhs) const
    {
        if (this == &rhs)
            return true;

        return mState == rhs.mState &&
            (
            (mState == done) ||
            (mState == delNodes && mDelIter == rhs.mDelIter) ||
            (mState != delNodes && mIter == rhs.mIter)
            );
    }

    bool LedgerDelta::MetaHelper::ValueIterator::operator!=(const LedgerDelta::MetaHelper::ValueIterator& rhs) const
    {
        return !(*this == rhs);
    }

    LedgerDelta::MetaHelper::value_type const& LedgerDelta::MetaHelper::ValueIterator::operator*()
    {
        // copies are still occuring but can be avoided by splitting
        // the live from dead entries (thus only using LedgerEntry types)
        switch (mState)
        {
        case newNodes:
        case modNodes:
            mCurObject.entry.type(LIVEENTRY);
            mCurObject.entry.liveEntry() = mIter->second->mEntry;
            return mCurObject;
        case delNodes:
            mCurObject.entry.type(DEADENTRY);
            mCurObject.entry.deadEntry() = *mDelIter;
            return mCurObject;
        default:
            throw std::out_of_range("could not read past last element");
        }
    }
}
