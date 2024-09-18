
#include <regex>

#include <data/io/exception.hpp>
#include <data/crypto/NIST_DRBG.hpp>

#include <data/io/wait_for_enter.hpp>

#include <gigamonkey/timestamp.hpp>
#include <gigamonkey/script/pattern/pay_to_address.hpp>

#include <Cosmos/network.hpp>
#include <Cosmos/wallet/split.hpp>
#include <Cosmos/wallet/restore.hpp>
#include <Cosmos/database/price_data.hpp>
#include <Cosmos/options.hpp>
#include <Cosmos/tax.hpp>
#include <Cosmos/boost/miner_options.hpp>

#include "Cosmos.hpp"
#include "interface.hpp"

int main (int arg_count, char **arg_values) {

    auto err = run (arg_parser {arg_count, arg_values});

    if (err.Message) {
        if (err.Code) std::cout << "Error: ";
        std::cout << static_cast<std::string> (*err.Message) << std::endl;
    } else if (err.Code) std::cout << "Error: unknown." << std::endl;

    return err.Code;
}

error run (const io::arg_parser &p) {

    try {

        if (p.has ("version")) version ();

        else if (p.has ("help")) help ();

        else {

            method cmd = read_method (p);

            switch (cmd) {
                case method::VERSION: {
                    version ();
                    break;
                }

                case method::HELP: {
                    help (read_method (p, 2));
                    break;
                }

                case method::GENERATE: {
                    command_generate (p);
                    break;
                }

                case method::VALUE: {
                    command_value (p);
                    break;
                }

                case method::RESTORE: {
                    command_restore (p);
                    break;
                }

                case method::UPDATE: {
                    command_update (p);
                    break;
                }

                case method::REQUEST: {
                    command_request (p);
                    break;
                }

                case method::ACCEPT: {
                    command_accept (p);
                    break;
                }

                case method::PAY: {
                    command_pay (p);
                    break;
                }

                case method::SIGN: {
                    command_sign (p);
                    break;
                }

                case method::SEND: {
                    command_send (p);
                    break;
                }

                case method::IMPORT: {
                    command_import (p);
                    break;
                }

                case method::SPLIT: {
                    command_split (p);
                    break;
                }

                case method::BOOST: {
                    command_boost (p);
                    break;
                }

                case method::TAXES: {
                    command_taxes (p);
                    break;
                }

                default: {
                    std::cout << "Error: could not read user's command." << std::endl;
                    help ();
                }
            }
        }

    } catch (const net::HTTP::exception &x) {
        std::cout << "Problem with http: " << std::endl;
        std::cout << "\trequest: " << x.Request << std::endl;
        std::cout << "\tresponse: " << x.Response << std::endl;
        return error {1, std::string {x.what ()}};
    } catch (const data::exception &x) {
        return error {x.Code, std::string {x.what ()}};
    } catch (const std::exception &x) {
        return error {1, std::string {x.what ()}};
    } catch (...) {
        return error {1};
    }

    return {};
}

std::string regex_replace (const std::string &x, const std::regex &r, const std::string &n) {
    std::stringstream ss;
    std::regex_replace (std::ostreambuf_iterator<char> (ss), x.begin (), x.end (), r, n);
    return ss.str ();
}

std::string sanitize (const std::string &in) {
    return regex_replace (data::to_lower (in), std::regex {"_|-"}, "");
}

method read_method (const arg_parser &p, uint32 index) {
    maybe<std::string> m;
    p.get (index, m);
    if (!bool (m)) return method::UNSET;

    std::transform (m->begin (), m->end (), m->begin (),
        [] (unsigned char c) {
            return std::tolower (c);
        });

    if (*m == "help") return method::HELP;
    if (*m == "version") return method::VERSION;
    if (*m == "generate") return method::GENERATE;
    if (*m == "restore") return method::RESTORE;
    if (*m == "value") return method::VALUE;
    if (*m == "request") return method::REQUEST;
    if (*m == "accept") return method::ACCEPT;
    if (*m == "pay") return method::PAY;
    if (*m == "sign") return method::SIGN;
    if (*m == "import") return method::IMPORT;
    if (*m == "send") return method::SEND;
    if (*m == "boost") return method::BOOST;
    if (*m == "split") return method::SPLIT;
    if (*m == "taxes") return method::TAXES;

    return method::UNSET;
}

void help (method meth) {
    switch (meth) {
        default : {
            version ();
            std::cout << "input should be <method> <args>... where method is "
                "\n\tgenerate   -- create a new wallet."
                "\n\tupdate     -- get Merkle proofs for txs that were pending last time the program ran."
                "\n\tvalue      -- print the total value in the wallet."
                "\n\trequest    -- generate a payment_request."
                "\n\tpay        -- create a transaction based on a payment request."
                "\n\treceive    -- accept a new transaction for a payment request."
                "\n\tsign       -- sign an unsigned transaction."
                "\n\timport     -- add a utxo to this wallet."
                "\n\tsend       -- send to an address or script. (depricated)"
                "\n\tboost      -- boost content."
                "\n\tsplit      -- split an output into many pieces"
                "\n\trestore    -- restore a wallet from words, a key, or many other options."
                "\nuse help \"method\" for information on a specific method"<< std::endl;
        } break;
        case method::GENERATE : {
            std::cout << "Generate a new wallet in terms of 24 words (BIP 39) or as an extended private key."
                "\narguments for method generate:"
                "\n\t(--name=)<wallet name>"
                "\n\t(--words) (use BIP 39)"
                "\n\t(--no_words) (don't use BIP 39)"
                "\n\t(--accounts=<uint32> (=10)) (how many accounts to pre-generate)"
                // TODO: as in method restore, a string should be accepted here which
                // could have "bitcoin" "bitcoin_cash" or "bitcoinSV" as its values,
                // ignoring case, spaces, and '_'.
                "\n\t(--coin_type=<uint32> (=0)) (value of BIP 44 coin_type)" << std::endl;
        } break;
        case method::VALUE : {
            std::cout << "Print the value in a wallet. No parameters." << std::endl;
        } break;
        case method::REQUEST : {
            std::cout << "Generate a new payment request."
                "\narguments for method request:"
                "\n\t(--name=)<wallet name>"
                "\n\t(--payment_type=\"pubkey\"|\"address\"|\"xpub\") (= \"address\")"
                "\n\t(--expires=<number of minutes before expiration>)"
                "\n\t(--memo=\"<explanation of the nature of the payment>\")"
                "\n\t(--amount=<expected amount of payment>)" << std::endl;
        } break;
        case method::PAY : {
            std::cout << "arguments for method pay not yet available." << std::endl;
        } break;
        case method::ACCEPT : {
            std::cout << "Accept a payment." << std::endl;
        } break;
        case method::SIGN : {
            std::cout << "arguments for method sign not yet available." << std::endl;
        } break;
        case method::IMPORT : {
            std::cout << "arguments for method import not yet available." << std::endl;
        } break;
        case method::SEND : {
            std::cout << "This method is DEPRICATED" << std::endl;
        } break;
        case method::BOOST : {
            std::cout << "arguments for method boost not yet available." << std::endl;
        } break;
        case method::SPLIT : {
            std::cout << "Split outputs in your wallet into many tiny outputs with small values over a triangular distribution. "
                "\narguments for method split:"
                "\n\t(--name=)<wallet name>"
                "\n\t(--address=)<address | xpub>"
                "\n\t(--max_look_ahead=)<integer> (= 10) ; (only used if parameter 'address' is provided as an xpub"
                "\n\t(--min_sats=<float>) (= 123456)"
                "\n\t(--max_sats=<float>) (= 5000000)"
                "\n\t(--mean_sats=<float>) (= 1234567) " << std::endl;
        } break;
        case method::RESTORE : {
            std::cout << "arguments for method restore:"
                "\n\t(--name=)<wallet name>"
                "\n\t(--key=)<xpub | xpriv>"
                "\n\t(--max_look_ahead=)<integer> (= 10)"
                "\n\t(--words=<string>)"
                "\n\t(--key_type=\"HD_sequence\"|\"BIP44_account\"|\"BIP44_master\") (= \"HD_sequence\")"
                "\n\t(--coin_type=\"Bitcoin\"|\"BitcoinCash\"|\"BitcoinSV\"|<integer>)"
                "\n\t(--wallet_type=\"RelayX\"|\"ElectrumSV\"|\"SimplyCash\"|\"CentBee\"|<string>)"
                "\n\t(--entropy=<string>)" << std::endl;
        }
    }

}

void version () {
    std::cout << "Cosmos Wallet version 0.0.1 alpha" << std::endl;
}

void command_value (const arg_parser &p) {
    Cosmos::Interface e {};
    Cosmos::read_account_and_txdb_options (e, p);
    e.update<void> (Cosmos::update_pending_transactions);
    auto w = e.wallet ();
    if (!bool (w)) throw exception {} << "could not read wallet";
    return Cosmos::display_value (*w);
}

// find all pending transactions and check if merkle proofs are available.
void command_update (const arg_parser &p) {
    Cosmos::Interface e {};
    Cosmos::read_account_and_txdb_options (e, p);
    e.update<void> (Cosmos::update_pending_transactions);
}


maybe<std::string> de_escape (string_view input) {

    if (!valid (input)) return {};
    string rt {};

    std::ostringstream decoded;

    for (std::size_t i = 0; i < input.size (); ++i) {
        if (input[i] == '\\') {
            if (i + 1 >= input.size ()) return {};
            i += 1;
        }

        decoded << input[i];
    }

    return {decoded.str ()};
}

void command_pay (const arg_parser &p) {
    using namespace Cosmos;
    Cosmos::Interface e {};
    Cosmos::read_wallet_options (e, p);

    // first look for a payment request.
    maybe<std::string> payment_request_string;
    p.get (3, "request", payment_request_string);

    // if we cannot find one, maybe there's an address.
    maybe<std::string> address_string;
    p.get ("address", address_string);

    // if there is no amount specified, check for one.
    maybe<int64> amount_sats;
    p.get ("amount", amount_sats);

    maybe<string> memo_input;
    p.get ("memo", memo_input);

    maybe<string> output;
    p.get ("output", output);

    payments::payment_request *pr;
    if (bool (payment_request_string)) {

        std::cout << "payment request inputed as " << *payment_request_string << std::endl;
        std::string payment_request = *de_escape (*payment_request_string);
        std::cout << "de escaped as " << payment_request << std::endl;

        pr = new payments::payment_request {payments::read_payment_request (JSON::parse (payment_request))};

        if (bool (pr->Value.Amount) && bool (amount_sats) && *pr->Value.Amount != Bitcoin::satoshi {*amount_sats})
            throw exception {} << "WARNING: amount provided in payment request and as an option and do not agree";

        if (!bool (pr->Value.Amount)) {
            if (!bool (amount_sats)) throw exception {} << "no amount provided";
            else pr->Value.Amount = Bitcoin::satoshi {*amount_sats};
        }

        if (bool (pr->Value.Memo) && bool (memo_input) && *pr->Value.Memo != *memo_input)
            throw exception {} << "WARNING: memo provided in payment request and as an option and do not agree";

        if (!bool (pr->Value.Memo) && bool (memo_input)) pr->Value.Memo = *memo_input;

    } else {
        if (!bool (address_string)) throw exception {} << "no address provided";
        if (!bool (amount_sats)) throw exception {} << "no amount provided";
        pr = new payments::payment_request {*address_string, payments::request {}};
        pr->Value.Amount = *amount_sats;
        if (bool (memo_input)) pr->Value.Memo = *memo_input;
    }

    // TODO if other incomplete payments exist, be sure to subtract
    // them from the account so as not to create a double spend.
    Bitcoin::satoshi value = e.wallet ()->value ();

    std::cout << "This is a payment for " << *pr->Value.Amount << " sats; wallet value: " << value << std::endl;

    // TODO estimate fee to send this tx.
    if (value < pr->Value.Amount) throw exception {} << "Wallet does not have sufficient funds to make this payment";

    // TODO encrypt payment request in OP_RETURN.
    BEEF beef = e.update<BEEF> ([pr] (Interface::writable u) -> BEEF {

        Bitcoin::address addr {pr->Key};
        Bitcoin::pubkey pubkey {pr->Key};
        HD::BIP_32::pubkey xpub {pr->Key};

        spend::spent spent;
        if (addr.valid ()) {
            spent = u.make_tx ({Bitcoin::output {*pr->Value.Amount, pay_to_address::script (addr.digest ())}});
        } else if (pubkey.valid ()) {
            spent = u.make_tx ({Bitcoin::output {*pr->Value.Amount, pay_to_pubkey::script (pubkey)}});
        } else if (xpub.valid ()) {
            throw exception {} << "pay to xpub not yet implemented";
        } else throw exception {} << "could not read payment address " << pr->Key;
        std::cout << " generating SPV proof " << std::endl;
        maybe<SPV::proof> ppp = generate_proof (*u.local_txdb (),
            for_each ([] (const auto &e) -> Bitcoin::transaction {
                return Bitcoin::transaction (e.first);
            }, spent.Transactions));

        if (!bool (ppp)) throw exception {} << "failed to generate payment";

        std::cout << "SPV proof generated containing " << ppp->Payment.size () <<
            " transactions and " << ppp->Proof.size () << " antecedents" << std::endl;

        BEEF beef {*ppp};
        std::cout << "Beef produced containing " << beef.Transactions.size () <<
            " transactions and " << beef.BUMPs.size () << " proofs" << std::endl;

        // save to proposed payments.
        auto payments = *u.get ().payments ();
        u.set_payments (Cosmos::payments {payments.Requests, payments.Proposals.insert (pr->Key, payments::offer
            {*pr, beef, for_each ([] (const auto &e) -> account_diff {
                return e.second;
            }, spent.Transactions)})});

        return beef;
    });

    if (bool (output)) {
        // TODO make sure that the output doesn't already exist.
        write_to_file (encoding::base64::write (bytes (beef)), *output);
        std::cout << "offer written to " << *output << std::endl;
    } else std::cout << "please show this string to your seller and he will broadcast the payment if he accepts it:\n\t" <<
        encoding::base64::write (bytes (beef)) << std::endl;

    delete pr;
}

void command_sign (const arg_parser &p) {
    throw exception {} << "Commond sign not yet implemented";
}

// TODO get fee rate from options.
void command_send (const arg_parser &p) {
    std::cout << "WARNING: This command is depricated!" << std::endl;
    using namespace Cosmos;
    Cosmos::Interface e {};
    Cosmos::read_wallet_options (e, p);
    Cosmos::read_random_options (e, p);

    maybe<std::string> address_string;
    p.get (3, "address", address_string);
    if (!bool (address_string))
        throw exception {2} << "could not read address.";

    HD::BIP_32::pubkey xpub {*address_string};
    Bitcoin::address address {*address_string};

    maybe<int64> value;
    p.get (4, "value", value);
    if (!bool (value))
        throw exception {2} << "could not read value to send.";

    Bitcoin::satoshi spend_amount {*value};

    auto *rand = e.random ();
    auto *net = e.net ();

    if (rand == nullptr) throw exception {4} << "could not initialize random number generator.";
    if (net == nullptr) throw exception {5} << "could not connect to remote servers";

    e.update<void> (update_pending_transactions);
    // TODO update history
    if (address.valid ()) e.update<void> ([rand, net, &address, &spend_amount] (Cosmos::Interface::writable u) {
            spend::spent spent = u.make_tx ({Bitcoin::output {spend_amount, pay_to_address::script (address.decode ().Digest)}});
            for (const auto &[extx, diff] : spent.Transactions) u.broadcast (extx, diff);
            u.set_addresses (spent.Addresses);
        });
    else if (xpub.valid ()) e.update<void> ([rand, net, &xpub, &spend_amount] (Cosmos::Interface::writable u) {
            spend::spent spent = u.make_tx (for_each ([] (const redeemable &m) -> Bitcoin::output {
                    return m.Prevout;
                }, Cosmos::split {} (*rand, address_sequence {xpub, {}, 0}, spend_amount, .001).Outputs));
            for (const auto &[extx, diff] : spent.Transactions) u.broadcast (extx, diff);
            u.set_addresses (spent.Addresses);
        });
    else throw exception {2} << "Could not read address/xpub";
}

void command_boost (const arg_parser &p) {
    using namespace BoostPOW;

    Cosmos::Interface e {};
    Cosmos::read_wallet_options (e, p);
    Cosmos::read_random_options (e, p);

    maybe<int64> value;
    p.get (3, "value", value);
    if (!bool (value))
        throw exception {2} << "could not read value to boost.";

    Bitcoin::output op {Bitcoin::satoshi {*value}, Boost::output_script (script_options::read (p.Parser, 3)).write ()};
    // TODO update history
    e.update<void> (Cosmos::update_pending_transactions);
    e.update<void> ([net = e.net (), rand = e.random (), &op] (Cosmos::Interface::writable u) {
        Cosmos::spend::spent x = u.make_tx ({op});
        for (const auto &[extx, diff] : x.Transactions) u.broadcast (extx, diff);
        u.set_addresses (x.Addresses);
    });
}

void command_taxes (const arg_parser &p) {
    using namespace Cosmos;

    math::signed_limit<Bitcoin::timestamp> begin = math::signed_limit<Bitcoin::timestamp>::negative_infinity ();
    math::signed_limit<Bitcoin::timestamp> end = math::signed_limit<Bitcoin::timestamp>::infinity ();

    // first need to get a time range.
    // TODO maybe it should be possible to make a narrower time range.
    maybe<uint32> tax_year;
    p.get ("tax_year", tax_year);
    if (!bool (tax_year)) throw exception {1} << "no tax year given";

    std::tm tm_begin = {0, 0, 0, 1, 0, *tax_year - 1900};
    std::tm tm_end = {0, 0, 0, 1, 0, *tax_year - 1900 + 1};

    begin = Bitcoin::timestamp {std::mktime (&tm_begin)};
    end = Bitcoin::timestamp {std::mktime (&tm_begin)};

    // TODO there are also issues related to time zone
    // and the tax year.

    Interface e {};
    read_watch_wallet_options (e, p);

    // then look in history for that time range.
    e.update<void> ([&begin, &end] (Cosmos::Interface::writable u) {
        auto *h = u.history ();
        if (h == nullptr) throw exception {} << "could not read wallet history";

        std::cout << "Tax implications: " << std::endl;
        std::cout << tax::calculate (*u.txdb (), u.price_data (), h->get_history (begin, end)) << std::endl;
    });
}
