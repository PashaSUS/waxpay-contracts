#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <string>

using namespace eosio;

// Main contract: manages whitelisted tokens and their metadata
class [[eosio::contract("twl.waxpayio")]] tokenwhitelist : public contract {
public:
    using contract::contract;

    // External stat table to validate token info from its original contract
    struct currency_stats {
        asset supply;
        asset max_supply;
        name issuer;
        uint64_t primary_key() const { return supply.symbol.code().raw(); }
    };

    typedef multi_index<"stat"_n, currency_stats> stats_table;

    // Action: Add a token to the whitelist
    [[eosio::action]]
    void addtoken(name contract, symbol symbol, std::string image_link, double system_fee) {
        require_auth(get_self());
        check(system_fee >= 0, "System fee cannot be negative");

        tokens_table tokens(get_self(), get_self().value);

        // Ensure token is not already whitelisted
        auto existing = std::find_if(tokens.begin(), tokens.end(), [&](const auto& row) {
            return row.contract == contract && row.symbol == symbol;
        });
        check(existing == tokens.end(), "Token contract already whitelisted");

        // Check symbol exists in the token's contract
        stats_table stats(contract, symbol.code().raw());
        const auto& stat = stats.get(symbol.code().raw(), "Invalid token symbol");
        check(symbol == stat.supply.symbol, "Symbol precision mismatch");

        // Add to whitelist
        tokens.emplace(get_self(), [&](auto& row) {
            row.id = tokens.available_primary_key();
            row.contract = contract;
            row.symbol = symbol;
            row.image_link = image_link;
            row.system_fee = system_fee;
            row.slippage = 0.0; // default slippage
        });
    }

    // Action: Change system fee for a token
    [[eosio::action]]
    void changesysfee(uint64_t id, double system_fee) {
        require_auth(get_self());
        check(system_fee >= 0, "System fee cannot be negative");

        tokens_table tokens(get_self(), get_self().value);
        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token doesn't exist.");

        tokens.modify(itr, same_payer, [&](auto& row) {
            row.system_fee = system_fee;
        });
    }

    // Action: Change token image URL
    [[eosio::action]]
    void changeimage(uint64_t id, std::string image_link) {
        require_auth(get_self());

        tokens_table tokens(get_self(), get_self().value);
        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token doesn't exist.");

        tokens.modify(itr, same_payer, [&](auto& row) {
            row.image_link = image_link;
        });
    }

    // Action: Remove a token from whitelist
    [[eosio::action]]
    void rmtoken(uint64_t id) {
        require_auth(get_self());

        tokens_table tokens(get_self(), get_self().value);
        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token not found");

        tokens.erase(itr);

        // Notify store whitelist contract
        action(
            permission_level{get_self(), "active"_n},
            STORE_WHITELIST,
            "rmvsystoken"_n,
            std::make_tuple(id)
        ).send();
    }

    // Action: Clear all tokens from whitelist (dev/admin only)
    [[eosio::action]]
    void cls() {
        require_auth(get_self());

        tokens_table tokens(get_self(), get_self().value);
        auto itr = tokens.begin();
        while (itr != tokens.end()) {
            itr = tokens.erase(itr);
        }
    }

    // Action: Add or change slippage value for a token
    [[eosio::action]]
    void addslippage(uint64_t id, double slippage) {
        require_auth(get_self());
        check(slippage >= 0, "Slippage cannot be negative");

        tokens_table tokens(get_self(), get_self().value);
        auto itr = tokens.find(id);
        check(itr != tokens.end(), "Token not found");

        tokens.modify(itr, same_payer, [&](auto& row) {
            row.slippage = slippage;
        });
    }

private:
    const name TOKEN_WHITELIST = "twl.waxpayio"_n;
    const name STORE_WHITELIST = "swl.waxpayio"_n;

    // Token metadata stored in the contract
    struct [[eosio::table]] tokens {
        uint64_t id;
        name contract;
        symbol symbol;
        std::string image_link;
        double system_fee;
        double slippage;

        uint64_t primary_key() const { return id; }
    };

    using tokens_table = multi_index<"tokens"_n, tokens>;
};
