//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class FakeBook {
    std::string base_;
    std::string first_;

public:
    std::string*
    mutableFirstBook()
    {
        return &first_;
    }

    std::string
    bookBase() const
    {
        return base_;
    }

    std::string*
    mutableBookBase()
    {
        return &base_;
    }
};

class FakeBookSuccessors {
    std::vector<FakeBook> books_;

public:
    auto
    begin()
    {
        return books_.begin();
    }

    auto
    end()
    {
        return books_.end();
    }
};

class FakeLedgerObject {
public:
    enum ModType : int { MODIFIED, DELETED };

private:
    std::string key_;
    std::string data_;
    std::string predecessor_;
    std::string successor_;
    ModType mod_ = MODIFIED;

public:
    ModType
    modType() const
    {
        return mod_;
    }

    std::string
    key() const
    {
        return key_;
    }

    std::string*
    mutableKey()
    {
        return &key_;
    }

    std::string
    data() const
    {
        return data_;
    }

    std::string*
    mutableData()
    {
        return &data_;
    }

    std::string*
    mutablePredecessor()
    {
        return &predecessor_;
    }

    std::string*
    mutableSuccessor()
    {
        return &successor_;
    }
};

class FakeLedgerObjects {
    std::vector<FakeLedgerObject> objects_;

public:
    std::vector<FakeLedgerObject>*
    mutableObjects()
    {
        return &objects_;
    }
};

class FakeTransactionsList {
    std::size_t size_ = 0;

public:
    std::size_t
    transactionsSize() const
    {
        return size_;
    }
};

class FakeObjectsList {
    std::size_t size_ = 0;

public:
    std::size_t
    objectsSize() const
    {
        return size_;
    }
};

struct FakeFetchResponse {
    uint32_t id;
    bool objectNeighborsIncluded;
    FakeLedgerObjects ledgerObjects;
    std::string ledgerHeader;
    FakeBookSuccessors bookSuccessors;

    FakeFetchResponse(uint32_t id = 0, bool objectNeighborsIncluded = false)
        : id{id}, objectNeighborsIncluded{objectNeighborsIncluded}
    {
    }

    FakeFetchResponse(std::string blob, uint32_t id = 0, bool objectNeighborsIncluded = false)
        : id{id}, objectNeighborsIncluded{objectNeighborsIncluded}, ledgerHeader{std::move(blob)}
    {
    }

    bool
    operator==(FakeFetchResponse const& other) const
    {
        return other.id == id;
    }

    static FakeTransactionsList
    transactionsList()
    {
        return {};
    }

    static FakeObjectsList
    ledgerObjects()
    {
        return {};
    }

    bool
    objectNeighborsIncluded() const
    {
        return objectNeighborsIncluded;
    }

    FakeLedgerObjects*
    mutableLedgerObjects()
    {
        return &ledgerObjects;
    }

    std::string
    ledgerHeader() const
    {
        return ledgerHeader;
    }

    std::string*
    mutableLedgerHeader()
    {
        return &ledgerHeader;
    }

    FakeBookSuccessors*
    mutableBookSuccessors()
    {
        return &bookSuccessors;
    }
};
