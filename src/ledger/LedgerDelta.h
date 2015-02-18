#pragma once

#include <map>
#include <set>
#include "ledger/EntryFrame.h"
#include "clf/LedgerCmp.h"
#include "xdrpp/marshal.h"

namespace stellar
{
    class LedgerDelta
    {
        typedef std::map<LedgerKey, EntryFrame::pointer, LedgerEntryIdCmp> EntryMap;
        typedef std::set<LedgerKey, LedgerEntryIdCmp> KeySet;
        EntryMap mNew;
        EntryMap mMod;
        KeySet mDelete;

        void addEntry(EntryFrame::pointer entry);
        void deleteEntry(EntryFrame::pointer entry);
        void modEntry(EntryFrame::pointer entry);

    public:

        void addEntry(EntryFrame const& entry);
        void deleteEntry(EntryFrame const& entry);
        void deleteEntry(LedgerKey const& key);
        void modEntry(EntryFrame const& entry);

        // apply other on top of delta, collapsing entries as appropriate
        void merge(LedgerDelta &other);

        xdr::msg_ptr getTransactionMeta() const;

        // helper class to serialize LedgerDelta without duplicating too much data
        class MetaHelper
        {
            const LedgerDelta& mLedgerDelta;
        public:
            MetaHelper(const LedgerDelta &ld) : mLedgerDelta(ld) {}
            using value_type = CLFEntryBase;
            Constexpr const std::size_t size() const;
            void validate() const {}
            void check_size(uint32_t i) const;
            void resize(uint32_t i);
            value_type &extend_at(uint32_t i);

            class ValueIterator : public std::iterator<std::input_iterator_tag, value_type const>
            {
                CLFEntryBase mCurObject;
                LedgerDelta const& mLedgerDelta;
                EntryMap::const_iterator mIter;
                KeySet::const_iterator mDelIter;

                enum { newNodes, modNodes, delNodes, done } mState;
            public:
                ValueIterator(MetaHelper const& me, bool begin);
                
                ValueIterator& operator++();
                bool operator==(const ValueIterator& rhs) const;
                bool operator!=(const ValueIterator& rhs) const;
                value_type const& operator*();
            };

            ValueIterator begin() const;
            ValueIterator end() const;

        };
    };
}

namespace xdr
{
    template<> struct xdr_traits<::stellar::LedgerDelta::MetaHelper> : detail::xdr_container_base<::stellar::LedgerDelta::MetaHelper, true, false>
    {
    };
}
