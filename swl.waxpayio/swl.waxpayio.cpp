#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <string>

using namespace eosio;

// Contract: swl.waxpayio (Store Whitelist)
class [[eosio::contract("swl.waxpayio")]] storewhitelist : public contract {
public:
    using contract::contract;

    // Token whitelist structure (linked to system-wide whitelist)
    struct twl_tokens {
        uint64_t id;
        name contract;
        symbol symbol;
        double system_fee;
        double slippage;
        uint64_t primary_key() const { return id; }
    };
    using twl_tokens_table = eosio::multi_index<"tokens"_n, twl_tokens>;

    // Action: Add a new store to the whitelist
    [[eosio::action]]
    void addstore(std::string store_id, std::string store_name, name authenticated_account) {
        require_auth(get_self());
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action");

        stores_table stores(get_self(), get_self().value);

        // Ensure store_id and account aren't already used
        for (auto itr = stores.begin(); itr != stores.end(); itr++) {
            check(!(itr->store_id == store_id), "Store already registered");
            check(!(itr->authenticated_account == authenticated_account), "Account already authenticated with another store");
        }

        // Save the new store
        stores.emplace(get_self(), [&](auto &row) {
            row.id = stores.available_primary_key();
            row.store_id = store_id;
            row.store_name = store_name;
            row.authenticated_account = authenticated_account;
        });
    }

    // Action: Add a revenue-sharing recipient for the store
    [[eosio::action]]
    void addrecipient(name user, name recipient, uint8_t weight) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        // Verify store ownership
        auto user_id = check_authorized(user);
        recipients_table recipients(get_self(), user_id);

        // Ensure the recipient is not already registered
        auto itr = recipients.find(recipient.value);
        check(itr == recipients.end(), "Recipient already exists");

        // Add recipient
        recipients.emplace(user, [&](auto &row) {
            row.recipient = recipient;
            row.weight = weight;
        });
    }

#pragma region remove recipients

    // Action: Remove all recipients from a store
    [[eosio::action]]
    void rmvrecs(name user) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        auto user_id = check_authorized(user);
        recipients_table recipients(get_self(), user_id);
        auto itr = recipients.begin();

        while (itr != recipients.end())
            itr = recipients.erase(itr);
    }

    // Action: Remove a single recipient
    [[eosio::action]]
    void rmvrec(name user, name recipient) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        auto user_id = check_authorized(user);
        recipients_table recipients(get_self(), user_id);
        auto itr = recipients.find(recipient.value);
        check(itr != recipients.end(), "Recipient doesn't exist");

        recipients.erase(itr);
    }

#pragma endregion

#pragma region token related

    // Action: Add a token to the store's supported tokens list
    [[eosio::action]]
    void addtoken(name user, uint64_t id, double min_slippage = 0.00, double max_slippage = 100.00, double usd_value = 0.00) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");
        check(min_slippage <= max_slippage, "min cannot be larger than max");

        auto user_id = check_authorized(user);
        tokens_table tokens(get_self(), user_id);

        // Prevent duplicate
        auto itr = tokens.find(id);
        check(itr == tokens.end(), "Token already added");

        // Ensure it's on the system whitelist
        twl_tokens_table twl_tokens(TOKEN_WHITELIST, TOKEN_WHITELIST.value);
        auto twl_token = twl_tokens.find(id);
        check(twl_token != twl_tokens.end(), "Token not whitelisted");

        // Add the token to the store
        tokens.emplace(user, [&](auto &row) {
            row.id = id;
            row.min_slippage = min_slippage;
            row.max_slippage = max_slippage;
            row.usd_value = usd_value;
        });
    }

    // Action: Edit store-supported token
    [[eosio::action]]
    void edittoken(name user, uint64_t id, double min_slippage = 0.00, double max_slippage = 100.00, double usd_value = 0.00) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");
        check(min_slippage <= max_slippage, "min cannot be larger than max");

        auto user_id = check_authorized(user);
        tokens_table tokens(get_self(), user_id);

        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token doesn't exist");

        tokens.modify(itr, same_payer, [&](auto &row) {
            row.min_slippage = min_slippage;
            row.max_slippage = max_slippage;
            row.usd_value = usd_value;
        });
    }

    // Action: Toggle token active/inactive status
    [[eosio::action]]
    void changestate(name user, uint64_t id, bool active) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        auto user_id = check_authorized(user);
        tokens_table tokens(get_self(), user_id);
        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token doesn't exist");

        tokens.modify(itr, same_payer, [&](auto &row) {
            row.active = active;
        });
    }

    // Action: Remove a specific token from a store
    [[eosio::action]]
    void rmvtoken(name user, uint64_t id) {
        require_auth(user);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        auto user_id = check_authorized(user);
        tokens_table tokens(get_self(), user_id);
        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token doesn't exist");

        tokens.erase(itr);
    }

    // Action: Admin force-remove a system token from all stores
    [[eosio::action]]
    void rmvsystoken(uint64_t id) {
        require_auth(TOKEN_WHITELIST);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        stores_table stores(get_self(), get_self().value);
        for (auto store_itr = stores.begin(); store_itr != stores.end(); store_itr++) {
            tokens_table tokens(get_self(), store_itr->id);
            auto token = tokens.find(id);
            tokens.erase(token);
        }
    }

#pragma endregion

    // Action: Clear all stores and their associated data
    [[eosio::action]]
    void cls() {
        require_auth(get_self());
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        stores_table stores(get_self(), get_self().value);
        auto itr = stores.begin();

        while (itr != stores.end()) {
            // Erase all recipients for this store
            recipients_table recipients(get_self(), itr->id);
            auto itr2 = recipients.begin();
            while (itr2 != recipients.end())
                itr2 = recipients.erase(itr2);

            // Erase all tokens for this store
            tokens_table tokens(get_self(), itr->id);
            auto itr3 = tokens.begin();
            while (itr3 != tokens.end())
                itr3 = tokens.erase(itr3);

            // Erase the store itself
            itr = stores.erase(itr);
        }
    }

    /*
    // Optional: Migration action for older token table format
    [[eosio::action]]
    void migrate() {
        require_auth(STORE_WHITELIST);
        check(get_self() == STORE_WHITELIST, "Only the correct contract can execute this action.");

        stores_table stores(get_self(), get_self().value);
        for (auto store_itr = stores.begin(); store_itr != stores.end(); store_itr++) {
            tokenmigrate_table tokenmigrate(get_self(), store_itr->id); // Old table
            tokens_table tokens(get_self(), store_itr->id);             // New table

            for (auto token_itr = tokenmigrate.begin(); token_itr != tokenmigrate.end();) {
                tokens.emplace(get_self(), [&](auto &row) {
                    row.id = token_itr->id;
                    row.min_slippage = token_itr->min_slippage;
                    row.max_slippage = token_itr->max_slippage;
                    row.active = token_itr->active;
                    row.usd_value = 0.00; // Optional or default
                });
                token_itr = tokenmigrate.erase(token_itr);
            }
        }
    }
    */

private:
    // Constant names for authority and table scoping
    const name TOKEN_WHITELIST = "twl.waxpayio"_n;
    const name STORE_WHITELIST = "swl.waxpayio"_n;

    // Internal helper: ensures user is tied to a registered store
    uint64_t check_authorized(name user) {
        stores_table stores(get_self(), get_self().value);
        auto itr = stores.begin();
        while (itr != stores.end() && itr->authenticated_account != user) {
            itr++;
        }
        check(itr != stores.end(), "This account isn't authorized to work with a store");
        return itr->id;
    }

    // === Table Definitions ===

    // Store registry
    struct [[eosio::table]] stores {
        uint64_t id;
        std::string store_id;
        std::string store_name;
        name authenticated_account;
        uint64_t primary_key() const { return id; }
    };
    using stores_table = multi_index<"stores"_n, stores>;

    // Store-specific revenue recipients
    struct [[eosio::table]] recipients {
        name recipient;
        uint8_t weight;  // Used for split percentage
        uint64_t primary_key() const { return recipient.value; }
    };
    using recipients_table = multi_index<"recipients"_n, recipients>;

    // Store-specific token settings
    struct [[eosio::table]] tokens {
        uint64_t id;
        double min_slippage = 0;
        double max_slippage = 100;
        bool active = true;
        double usd_value = 0.00;
        uint64_t primary_key() const { return id; }
    };
    using tokens_table = multi_index<"tokens"_n, tokens>;

    /*
    // Old format (for migration)
    struct [[eosio::table]] tokenmigrate {
        uint64_t id;
        double min_slippage = 0;
        double max_slippage = 100;
        bool active = true;
        uint64_t primary_key() const { return id; };
    };
    using tokenmigrate_table = multi_index<"tokenmigrate"_n, tokenmigrate>;
    */
};
