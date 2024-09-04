
#include <data/io/exception.hpp>
#include <data/crypto/NIST_DRBG.hpp>
#include <gigamonkey/schema/bip_44.hpp>
#include <gigamonkey/schema/bip_39.hpp>
#include <Cosmos/network.hpp>
#include <Cosmos/interface.hpp>
#include <Cosmos/wallet/split.hpp>

namespace Cosmos {

    void generate_new_xpub (pubkeychain &p) {
        address_sequence receive_sequence = p.Sequences[p.Receive];
        HD::BIP_32::pubkey next_pubkey = receive_sequence.Key.derive (receive_sequence.Path << receive_sequence.Last);
        Bitcoin::address::decoded next_address = next_pubkey.address ();
        std::cout << "next xpub is " << next_pubkey << " and its address is " << next_address;
        p = p.next (p.Receive);
    }

    void generate_wallet::operator () (Interface::writable u) {
        auto *w = u.wallet ();
        if (w == nullptr) throw exception {} << "could not read wallet.";

        std::cout << "We will generate a new BIP 44 wallet for you with " << Accounts << " accounts pre-generated" << std::endl;
        std::cout << "Coin type is Bitcoin (= 0)" << std::endl;
        std::cout << "Type random characters to use as entropy for generating this wallet. "
            "Press enter when you think you have enough."
            "Around 200 characters ought to be enough as long as they are random enough." << std::endl;

        std::string user_input {};

        while (true) {
            char x = std::cin.get ();
            if (x == '\n') break;
            user_input.push_back (x);
        }

        digest512 bits = crypto::SHA2_512 (user_input);

        std::string words = HD::BIP_39::generate (bits);

        std::cout << "your words are\n\n\t" << words << "\n\nRemember, these words can be used to generate "
            "all your keys, but at scale that is not enough to restore your funds. You need to keep the transactions"
            " into your wallet along with Merkle proofs as well." << std::endl;

        // TODO use passphrase option.
        HD::BIP_32::secret master = HD::BIP_32::secret::from_seed (HD::BIP_39::read (words));

        HD::BIP_32::pubkey master_pubkey = master.to_public ();
        keychain key = keychain {}.insert (master_pubkey, master);

        pubkeychain pub {{}, {}, "receive_0", "change_0"};

        for (int account = 0; account < Accounts; account++) {
            list<uint32> path {
                HD::BIP_44::purpose,
                HD::BIP_44::coin_type_Bitcoin,
                HD::BIP_32::harden (account)};

            HD::BIP_32::pubkey account_master_pubkey = master.derive (path).to_public ();

            std::cout << "\tmaster pubkey for account " << account << " is " << account_master_pubkey << std::endl;

            pub.Derivations = pub.Derivations.insert (account_master_pubkey, derivation {master_pubkey, path});

            std::string receive_name = std::string {"receive_"} + std::to_string (account);
            std::string change_name = std::string {"change_"} + std::to_string (account);

            pub.Sequences = pub.Sequences.insert (receive_name, address_sequence {account_master_pubkey, {HD::BIP_44::receive_index}});
            pub.Sequences = pub.Sequences.insert (change_name, address_sequence {account_master_pubkey, {HD::BIP_44::change_index}});
        }
    }

    void send_to (network &n, wallet &w, crypto::random &rand, list<Bitcoin::output> o) {
        spent z = spend (w,
            select_output_parameters {4, 5000, .23},
            make_change_parameters {10, 100, 1000000, .4},
            Gigamonkey::redeem_p2pkh_and_p2pk, rand, o);

        std::cout << "broadcasting tx " << z.Transaction.id () << std::endl;

        bytes tx_raw = bytes (z.Transaction);

        auto err = n.broadcast (tx_raw);

        if (bool (err)) throw exception {3} << "tx broadcast failed;\n\ttx: " << encoding::hex::write (tx_raw) << "\n\terror: " << err;

        w = z.Wallet;
    }

    void read_both_chains_options (Interface &e, const arg_parser &p) {
        auto &name = e.wallet_name ();
        p.get (2, "name", name);
        if (bool (name)) return;

        auto &keychain_filepath = e.keychain_filepath ();
        auto &pubkeychain_filepath = e.pubkeychain_filepath ();
        p.get ("keychain_filepath", keychain_filepath);
        p.get ("pubkeychain_filepath", pubkeychain_filepath);
        if (!bool (keychain_filepath)) throw data::exception {1} << "could not read filepath of keychain";
        if (!bool (pubkeychain_filepath)) throw data::exception {1} << "could not read filepath of pubkeychain";
    }

    void read_pubkeychain_options (Interface &e, const arg_parser &p) {
        auto &name = e.wallet_name ();
        p.get (2, "name", name);
        if (bool (name)) return;

        auto &filepath = e.pubkeychain_filepath ();
        p.get ("filepath", filepath);
        if (bool (filepath)) return;
        p.get ("pubkeychain_filepath", filepath);
        if (bool (filepath)) return;

        throw data::exception {1} << "could not read filepath of pubkeychain";
    }

    void read_account_and_txdb_options (Interface &e, const arg_parser &p) {
        auto &name = e.wallet_name ();
        p.get (2, "name", name);
        if (bool (name)) return;

        auto &txdb_filepath = e.txdb_filepath ();
        auto &account_filepath = e.account_filepath ();
        p.get ("txdb_filepath", txdb_filepath);
        p.get ("account_filepath", account_filepath);
        if (!bool (txdb_filepath)) std::cout << "local tx database not provided; using remote only";
        if (!bool (account_filepath)) throw data::exception {1} << "could not read filepath of account";
    }

    void read_random_options (Interface &e, const arg_parser &p) {
        // no options set up for random yet.
    }

    maybe<std::string> &Interface::keychain_filepath () {
        if (!bool (KeychainFilepath) && bool (Name)) {
            std::stringstream ss;
            ss << *Name << ".keychain.json";
            KeychainFilepath = ss.str ();
        }

        return KeychainFilepath;
    }

    maybe<std::string> &Interface::pubkeychain_filepath () {
        if (!bool (PubkeychainFilepath) && bool (Name)) {
            std::stringstream ss;
            ss << *Name << ".pubkeychain.json";
            PubkeychainFilepath = ss.str ();
        }

        return PubkeychainFilepath;
    }

    maybe<std::string> &Interface::txdb_filepath () {
        if (!bool (TXDBFilepath) && bool (Name)) {
            std::stringstream ss;
            ss << "txdb.json";
            TXDBFilepath = ss.str ();
        }

        return TXDBFilepath;
    }

    maybe<std::string> &Interface::account_filepath () {
        if (!bool (AccountFilepath) && bool (Name)) {
            std::stringstream ss;
            ss << *Name << ".account.json";
            AccountFilepath = ss.str ();
        }

        return AccountFilepath;
    }

    maybe<std::string> &Interface::price_data_filepath () {
        if (!bool (PriceDataFilepath) && bool (Name)) {
            std::stringstream ss;
            ss << "price_data.json";
            PriceDataFilepath = ss.str ();
        }

        return PriceDataFilepath;
    }

    maybe<std::string> &Interface::history_filepath () {
        if (!bool (HistoryFilepath) && bool (Name)) {
            std::stringstream ss;
            ss << *Name << ".history.json";
            HistoryFilepath = ss.str ();
        }

        return HistoryFilepath;
    }

    network *Interface::net () {
        if (!bool (Net)) Net = new network ();

        return Net;
    }

    txdb *Interface::get_txdb () {
        if (bool (RemoteTXDB)) return RemoteTXDB;

        if (!bool (LocalTXDB)) {
            auto txf = txdb_filepath ();
            std::cout << "   " << "txdb filepath is " << txf << std::endl;
            if (bool (txf)) LocalTXDB = new JSON_local_txdb {read_JSON_local_txdb_from_file (*txf)};
            else return nullptr;
        }

        auto n = net ();
        if (bool (n)) {

            RemoteTXDB = new cached_remote_txdb {*n, *LocalTXDB};
            LocalTXDB = &RemoteTXDB->Local;
            return RemoteTXDB;

        } else return LocalTXDB;
    }

    SPV::database *Interface::get_spvdb () {
        if (bool (RemoteTXDB)) return &RemoteTXDB->Local;

        if (!bool (LocalTXDB)) {
            auto txf = txdb_filepath ();
            if (bool (txf)) LocalTXDB = new JSON_local_txdb {read_JSON_local_txdb_from_file (*txf)};
            else return nullptr;
        }

        auto n = net ();
        if (bool (n)) {

            RemoteTXDB = new cached_remote_txdb {*n, *LocalTXDB};
            LocalTXDB = &RemoteTXDB->Local;
            return &RemoteTXDB->Local;

        } else return LocalTXDB;
    }

    account *Interface::get_account () {
        if (!bool (Account)) {
            auto af = account_filepath ();
            if (bool (af)) {
                Account = new Cosmos::account {read_account_from_file (*af)};
            }
        }

        return Account;
    }

    pubkeychain *Interface::get_pubkeys () {
        if (!bool (Pubkeys)) {
            auto pf = pubkeychain_filepath ();
            if (bool (pf)) Pubkeys = new pubkeychain {read_pubkeychain_from_file (*pf)};
        }

        return Pubkeys;
    }

    keychain *Interface::get_keys () {
        if (!bool (Keys)) {
            auto kf = keychain_filepath ();
            if (bool (kf)) Keys = new keychain {read_keychain_from_file (*kf)};
        }

        return Keys;
    }

    price_data *Interface::get_price_data () {
        if (PriceData == nullptr) {
            if (LocalPriceData == nullptr) {
                auto pdf = price_data_filepath ();
                if (!bool (pdf)) return nullptr;
                LocalPriceData = new Cosmos::JSON_price_data {read_from_file (*pdf)};
            }

            auto n = net ();
            if (!bool (n)) return static_cast<Cosmos::price_data *> (LocalPriceData);
            PriceData = new Cosmos::cached_remote_price_data {*n, *LocalPriceData};
        }

        return PriceData;
    }

    watch_wallet *Interface::get_watch_wallet () {
        if (bool (WatchWallet)) return WatchWallet;

        auto txs = txdb ();

        auto acc = account ();

        auto pkc = pubkeys ();

        if (!bool (txs) || !bool (acc) || !bool (pkc)) return nullptr;

        WatchWallet = new Cosmos::watch_wallet {*acc, *pkc};
        Account = &WatchWallet->Account;
        Pubkeys = &WatchWallet->Pubkeys;

        return WatchWallet;
    }

    wallet *Interface::get_wallet () {
        if (!bool (Wallet)) return Wallet;

        auto watch = watch_wallet ();
        auto k = keys ();
        if (!bool (watch) || !bool (k)) return nullptr;

        Wallet = new Cosmos::wallet {*k, watch->Account, watch->Pubkeys};
        Keys = &Wallet->Keys;
        WatchWallet = Wallet;

        return Wallet;
    }

    events *Interface::get_history () {
        if (!bool (Events)) {
            auto hf = history_filepath ();
            if (bool (hf)) Events = new events {read_from_file (*hf)};
        }

        return Events;
    }

    crypto::random *Interface::random () {

        if (!Random) {

            Entropy = std::make_shared<crypto::user_entropy> (
                "We need some entropy for this operation. Please type random characters.",
                "Sufficient entropy provided.", std::cout, std::cin);

            Random = new crypto::NIST::DRBG {crypto::NIST::DRBG::Hash, *Entropy};
        }

        return Random;

    }

    Interface::~Interface () {

        auto tf = txdb_filepath ();
        auto af = account_filepath ();
        auto pf = pubkeychain_filepath ();
        auto kf = keychain_filepath ();
        auto pdf = price_data_filepath ();

        if (Written) {
            if (bool (tf) && bool (LocalTXDB)) write_to_file (JSON (dynamic_cast<JSON_local_txdb &> (*LocalTXDB)), *tf);
            if (bool (af) && bool (Account)) write_to_file (JSON (*Account), *af);
            if (bool (pf) && bool (Pubkeys)) write_to_file (JSON (*Pubkeys), *kf);
            if (bool (kf) && bool (Keys)) write_to_file (JSON (*Keys), *kf);
            if (bool (pdf) && bool (LocalPriceData)) write_to_file (JSON (dynamic_cast<JSON_price_data &> (*LocalPriceData)), *pdf);
        }

        delete PriceData;
        delete LocalPriceData;

        if (Wallet != nullptr) delete Wallet;
        else {
            delete Keys;
            if (WatchWallet != nullptr) delete WatchWallet;
            else {
                delete Account;
                delete Pubkeys;
            }
        }


        if (RemoteTXDB != nullptr) delete RemoteTXDB;
        else delete LocalTXDB;

        delete Net;

        delete Random;
    }

}