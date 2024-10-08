#ifndef COSMOS_DATABASE_TXDB
#define COSMOS_DATABASE_TXDB

#include <gigamonkey/SPV.hpp>
#include <Cosmos/database/write.hpp>
#include <Cosmos/network.hpp>

namespace Cosmos {
    using namespace data;
    namespace Bitcoin = Gigamonkey::Bitcoin;
    namespace Merkle = Gigamonkey::Merkle;
    namespace SPV = Gigamonkey::SPV;

    // a transaction with a complete Merkle proof.
    struct vertex : SPV::database::confirmed {
        using SPV::database::confirmed::confirmed;
        vertex (SPV::database::confirmed &&c) : SPV::database::confirmed {std::move (c)} {}

        std::strong_ordering operator <=> (const vertex &tx) const {
            if (!valid ()) throw exception {} << "unconfirmed or invalid tx.";
            return this->Confirmation <=> tx.Confirmation;
        }

        bool operator == (const vertex &tx) const {
            if (!valid ()) throw exception {} << "unconfirmed or invalid tx.";
            return *this->Transaction == *tx.Transaction && this->Confirmation == tx.Confirmation;
        }

        bool valid () const {
            return this->has_proof ();
        }

        explicit operator bool () {
            return valid ();
        }

        Bitcoin::timestamp when () const {
            if (this->Confirmation.Header.Timestamp == Bitcoin::timestamp {0}) throw exception {} << " Warning: " <<
                " tx " << this->Transaction->id () << " has header " << this->Confirmation.Header;
            return this->Confirmation.Header.Timestamp;
        }
    };

    enum class direction {
        in,
        out
    };

    // a tx with a complete merkle proof and an indication of a specific input or output.
    struct ray {
        // output or input.
        bytes Put;

        Bitcoin::timestamp When;

        // the index of the transaction in the block.
        uint64 Index;

        // outpoint or inpoint.
        Bitcoin::outpoint Point;

        // value received.
        maybe<Bitcoin::satoshi> Value;

        Bitcoin::satoshi value () const {
            return bool (Value) ? *Value : Bitcoin::output::value (Put);
        }

        Cosmos::direction direction () const {
            return bool (Value) ? Cosmos::direction::in : Cosmos::direction::out;
        }

        ray (Bitcoin::timestamp w, uint64 i, const Bitcoin::outpoint &op, const Bitcoin::output &o) :
            Put {bytes (o)}, When {w}, Index {i}, Point {op}, Value {} {}

        ray (Bitcoin::timestamp w, uint64 i, const inpoint &ip, const Bitcoin::input &in, const Bitcoin::satoshi &v) :
            Put {bytes (in)}, When {w}, Index {i}, Point {ip}, Value {v} {}

        ray (const vertex &t, const Bitcoin::outpoint &op):
            Put {bytes (t.Transaction->Outputs[op.Index])},
            When {t.when ()},
            Index {t.Confirmation.Path.Index},
            Point {op}, Value {} {}

        ray (const vertex &t, const inpoint &ip, const Bitcoin::satoshi &v):
            Put {bytes (t.Transaction->Inputs[ip.Index])},
            When {t.when ()},
            Index {t.Confirmation.Path.Index},
            Point {ip}, Value {v} {
            if (!Bitcoin::input {Put}.Reference.Digest.valid ()) throw exception {} << "WARNING: invalid input! Y";
        }

        std::strong_ordering operator <=> (const ray &e) const {
            auto compare_time = When <=> e.When;
            if (compare_time != std::strong_ordering::equal) return compare_time;
            auto compare_index = Index <=> e.Index;
            if (compare_index != std::strong_ordering::equal) return compare_index;
            if (direction () != e.direction ()) return direction () == direction::in ? std::strong_ordering::less : std::strong_ordering::greater;
            return Point.Index <=> e.Point.Index;
        }

        bool operator == (const ray &e) const {
            return Put == e.Put && When == e.When &&
                direction () == e.direction () && Point.Index == e.Point.Index;
        }

    };

    std::ostream inline &operator << (std::ostream &o, const ray &r) {
        return o << "\n\t" << r.Value << " " << (r.direction () == direction::in ? "received in " : "spent from ") << r.Point << std::endl;
    }

    // a database of transactions.
    struct txdb {

        virtual vertex operator [] (const Bitcoin::TXID &id) = 0;

        // all outputs for a given address.
        virtual ordered_list<ray> by_address (const Bitcoin::address &) = 0;
        virtual ordered_list<ray> by_script_hash (const digest256 &) = 0;
        virtual ptr<ray> redeeming (const Bitcoin::outpoint &) = 0;

        Bitcoin::output output (const Bitcoin::outpoint &p) {
            vertex tx = (*this)[p.Digest];
            if (!tx.valid ()) return {};
            return tx.Transaction->Outputs[p.Index];
        }

        Bitcoin::satoshi value (const Bitcoin::outpoint &p) {
            return output (p).Value;
        }

        virtual ~txdb () {}

    };

    struct local_txdb : txdb, SPV::database {

        vertex operator [] (const Bitcoin::TXID &id) final override {
            return vertex {this->tx (id)};
        }

        bool import_transaction (const Bitcoin::transaction &, const Merkle::path &, const Bitcoin::header &h);

        virtual void add_address (const Bitcoin::address &, const Bitcoin::outpoint &) = 0;
        virtual void add_script (const digest256 &, const Bitcoin::outpoint &) = 0;
        virtual void set_redeem (const Bitcoin::outpoint &, const inpoint &) = 0;

        virtual ~local_txdb () {}

    };

    struct cached_remote_txdb final : local_txdb {
        network &Net;
        local_txdb &Local;

        cached_remote_txdb (network &n, local_txdb &x): local_txdb {}, Net {n}, Local {x} {}

        const Bitcoin::header *header (const N &) const final override;

        const entry<N, Bitcoin::header> *latest () const final override;

        // get by hash or merkle root (need both)
        const entry<N, Bitcoin::header> *header (const digest256 &) const final override;

        // do we have a tx or merkle proof for a given tx?
        SPV::database::confirmed tx (const Bitcoin::TXID &) final override;

        const entry<N, Bitcoin::header> *insert (const N &height, const Bitcoin::header &h) final override;

        bool insert (const Merkle::proof &) final override;
        void insert (const Bitcoin::transaction &) final override;

        set<Bitcoin::TXID> pending () final override;
        void remove (const Bitcoin::TXID &) final override;

        ordered_list<ray> by_address (const Bitcoin::address &) final override;
        ordered_list<ray> by_script_hash (const digest256 &) final override;
        ptr<ray> redeeming (const Bitcoin::outpoint &) final override;

        void add_address (const Bitcoin::address &, const Bitcoin::outpoint &) final override;
        void add_script (const digest256 &, const Bitcoin::outpoint &) final override;
        void set_redeem (const Bitcoin::outpoint &, const inpoint &) final override;

        bool import_transaction (const Bitcoin::TXID &);
    };

    // Since we might end up trying to broadcast multiple txs at once
    // all coming from a single proof, we have a way of collecting all
    // results that come up for all broadcasts so that we can handle
    // the tx appropriately in the database.
    struct broadcast_tree_result : broadcast_multiple_result {
        using broadcast_multiple_result::broadcast_multiple_result;
        map<Bitcoin::TXID, broadcast_single_result> Sub;
        broadcast_tree_result (const broadcast_multiple_result &r, map<Bitcoin::TXID, broadcast_single_result> nodes = {}):
            broadcast_multiple_result {r}, Sub {nodes} {}
    };

    // go through a proof, check it, put all antecedents in the database and broadcast those
    // without merkle proofs, then broadcast the root level txs.
    broadcast_tree_result broadcast (cached_remote_txdb &, SPV::proof);

    void inline cached_remote_txdb::add_address (const Bitcoin::address &a, const Bitcoin::outpoint &o) {
        return Local.add_address (a, o);
    }

    void inline cached_remote_txdb::add_script (const digest256 &d, const Bitcoin::outpoint &o) {
        return Local.add_script (d, o);
    }

    void inline cached_remote_txdb::set_redeem (const Bitcoin::outpoint &o, const inpoint &i) {
        return Local.set_redeem (o, i);
    }

    const entry<N, Bitcoin::header> inline *cached_remote_txdb::insert (const N &height, const Bitcoin::header &h) {
        return Local.insert (height, h);
    }

    bool inline cached_remote_txdb::insert (const Merkle::proof &p) {
        return Local.insert (p);
    }

    void inline cached_remote_txdb::insert (const Bitcoin::transaction &tx) {
        return Local.insert (tx);
    }

    set<Bitcoin::TXID> inline cached_remote_txdb::pending () {
        return Local.pending ();
    }

    void inline cached_remote_txdb::remove (const Bitcoin::TXID &txid) {
        return Local.remove (txid);
    }

    const inline entry<N, Bitcoin::header> *cached_remote_txdb::latest () const {
        return Local.latest ();
    }
}

#endif
