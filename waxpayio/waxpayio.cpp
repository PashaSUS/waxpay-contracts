#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/time.hpp>
#include <eosio/system.hpp>
#include <string>
#include <algorithm>
#include <eosio/print.hpp>

using namespace eosio;

// Main contract class
class [[eosio::contract("waxpayio")]] waxpay : public contract
{
public:
    using contract::contract;

    // Constructor
    waxpay(name receiver, name code, datastream<const char *> ds) : contract(receiver, code, ds) {};

    // Token struct for whitelist
    struct tokens
    {
        uint64_t id;
        name contract;           // Token contract (e.g. "eosio.token")
        symbol symbol;           // Token symbol (e.g. WAX)
        std::string image_link;  // Optional: token image URL
        double system_fee;       // System fee percentage
        double slippage;         // Slippage allowed
        uint64_t primary_key() const { return id; }
    };
    using tokens_table = multi_index<"tokens"_n, tokens>;

    // Store-specific token support definition
    struct store_tokens
    {
        uint64_t id;
        double min_slippage = 0;
        double max_slippage = 100;
        uint64_t primary_key() const { return id; }
    };
    using store_tokens_table = multi_index<"tokens"_n, store_tokens>;

    // Recipient share structure for splitting
    struct recipients
    {
        name recipient;
        uint8_t weight; // Weight defines proportional share
        uint64_t primary_key() const { return recipient.value; }
    };
    using recipients_table = multi_index<"recipients"_n, recipients>;

    // Triggered automatically when the contract receives a transfer
    [[eosio::on_notify("*::transfer")]]
    void orderpaid(name from, name to, asset quantity, std::string memo)
    {
        // Only process if the transfer is to this contract and from a different user
        check(get_self() == CONTRACT_ACCOUNT, "Only the correct contract can receive tokens.");
        if (to != CONTRACT_ACCOUNT || from == get_self()) {
            return;
        }

        // Memo should contain a system identifier
        check(memo != "", "Memo should contain a unique id.");

        name token_contract = get_first_receiver(); // Get token contract sending the transfer

        // Validate token
        auto token = check_token(token_contract, quantity);

        // Store the order
        orders_table orders(get_self(), get_self().value);
        orders.emplace(get_self(), [&](auto &row) {
            row.id = orders.available_primary_key();
            row.system_id = memo;
            row.token_contract = token_contract;
            row.sender = from;
            row.asset = quantity;
            row.timestamp = current_time_point();
        });
    }

    // Dummy auth function for off-chain validation
    [[eosio::action]]
    void auth(name user, std::string auth_code)
    {
        require_auth(user);
        check(get_self() == CONTRACT_ACCOUNT, "Only the correct contract can run this action.");
    }

    // Accept order and distribute funds
    [[eosio::action]]
    void acceptorder(std::string system_id, uint64_t store_id, std::string memo)
    {
        require_auth(get_self());
        check(get_self() == CONTRACT_ACCOUNT, "Only the correct contract can run this action.");

        // Lookup order by system_id
        orders_table orders(get_self(), get_self().value);
        auto order = orders.begin();
        while (order != orders.end() && order->system_id != system_id) {
            order++;
        }
        check(order != orders.end(), "Order not found");

        // Fetch token and validate slippage
        auto token = check_token(order->token_contract, order->asset);
        double slippage = token->slippage;

        // Check if store supports the token
        store_tokens_table store_tokens(STORE_WHITELIST, store_id);
        auto token_itr = store_tokens.begin();
        while (token_itr != store_tokens.end() && token_itr->id != token->id) {
            token_itr++;
        }
        if (token_itr == store_tokens.end()) {
            deny_order(order->sender, order->token_contract, order->asset);
            orders.erase(order);
            return;
        }

        // Clamp slippage within store's limits
        slippage = std::max(token_itr->min_slippage, std::min(token_itr->max_slippage, slippage));

        // Load recipients for the store
        recipients_table recipients(STORE_WHITELIST, store_id);
        uint8_t total_weight = 0;
        for (auto rec = recipients.begin(); rec != recipients.end(); rec++) {
            total_weight += rec->weight;
        }

        // Calculate proportions
        double total_percentage = token->system_fee + slippage + 100.0;
        int64_t order_split_value = order->asset.amount / total_percentage;
        int64_t dev = order_split_value * token->system_fee;
        int64_t total = order->asset.amount - dev;

        // Validate dev fee
        check(dev > 0,
              ("System fee calculation error. "
               "Calculated fee: " +
               asset(dev, token->symbol).to_string() +
               ", Order amount: " + order->asset.to_string() +
               ", Total percentage: " + std::to_string(total_percentage) +
               ", Split value: " + asset(order_split_value, token->symbol).to_string())
                  .c_str());

        // Send system fee to fee account
        send_tokens(token->contract, FEE_ACCOUNT, asset(dev, token->symbol), "System fee.");

        // Distribute remaining to recipients by weight
        int64_t distributed_amount = 0;
        int64_t total_split_amount = total / total_weight;

        for (auto rec = recipients.begin(); rec != recipients.end(); rec++) {
            int64_t amount = total_split_amount * rec->weight;
            distributed_amount += amount;
            check(amount > 0, "Recipient amount is negative");
            send_tokens(token->contract, rec->recipient, asset(amount, token->symbol), memo);
        }

        // Handle any leftover due to rounding
        int64_t remaining = total - distributed_amount;
        print("Remaining: ", remaining);
        check(remaining >= 0, "Distribution mismatch: overdrawn balance.");
        if (remaining > 0) {
            send_tokens(token->contract, FEE_ACCOUNT, asset(remaining, token->symbol), "Remainder.");
        }

        // Remove processed order
        orders.erase(order);
    }

    // Reject order and refund user
    [[eosio::action]]
    void rejectorder(std::string system_id)
    {
        require_auth(get_self());
        check(get_self() == CONTRACT_ACCOUNT, "Only the correct contract can run this action.");

        orders_table orders(get_self(), get_self().value);
        auto order = orders.begin();
        while (order != orders.end() && order->system_id != system_id) {
            order++;
        }
        check(order != orders.end(), "Order not found");

        // Refund tokens to balance
        deny_order(order->sender, order->token_contract, order->asset);
        orders.erase(order);
    }

    // User can claim their rejected/refunded balances
    [[eosio::action]]
    void claim(name user)
    {
        require_auth(user);
        check(get_self() == CONTRACT_ACCOUNT, "Only the correct contract can run this action.");

        balances_table balances(get_self(), user.value);
        auto itr = balances.begin();
        while (itr != balances.end()) {
            send_tokens(itr->token_contract, user, itr->asset, "Balance claim");
            itr = balances.erase(itr);
        }
    }

    // Clear all orders (admin action)
    [[eosio::action]]
    void cls()
    {
        require_auth(get_self());
        check(get_self() == CONTRACT_ACCOUNT, "Only the correct contract can run this action.");

        orders_table orders(get_self(), get_self().value);
        auto itr = orders.begin();
        while (itr != orders.end()) {
            itr = orders.erase(itr);
        }
    }

private:
    // Constants used in the contract
    const name TOKEN_WHITELIST = "twl.waxpayio"_n;
    const name STORE_WHITELIST = "swl.waxpayio"_n;
    const name CONTRACT_ACCOUNT = "waxpayio"_n;
    const name FEE_ACCOUNT = "fee.waxpayio"_n;

    // Move tokens to a user’s internal balance
    void deny_order(name sender, name token_contract, asset quantity)
    {
        balances_table balances(get_self(), sender.value);
        balances.emplace(get_self(), [&](auto &row) {
            row.id = balances.available_primary_key();
            row.token_contract = token_contract;
            row.asset = quantity;
        });
    }

    // Validate if token is whitelisted
    tokens_table::const_iterator check_token(name token_contract, asset quantity)
    {
        tokens_table tokens(TOKEN_WHITELIST, TOKEN_WHITELIST.value);
        auto itr = tokens.begin();
        while (itr != tokens.end() && itr->symbol != quantity.symbol && itr->contract != token_contract) {
            itr++;
        }
        check(itr != tokens.end(), "Token not whitelisted");
        return itr;
    }

    // Perform inline transfer
    void send_tokens(name contract, name recipient, asset amount, std::string memo)
    {
        action(permission_level{get_self(), "active"_n}, contract, "transfer"_n,
               std::make_tuple(get_self(), recipient, amount, memo)).send();
    }

    // Table to store user orders
    struct [[eosio::table]] orders
    {
        uint64_t id;
        std::string system_id;     // Unique identifier (from memo)
        name sender;
        name token_contract;
        asset asset;
        time_point_sec timestamp;
        uint64_t primary_key() const { return id; }
    };
    using orders_table = multi_index<"orders"_n, orders>;

    // Table to store unclaimed balances (e.g. rejected/refunded orders)
    struct [[eosio::table]] balances
    {
        uint64_t id;
        name token_contract;
        asset asset;
        uint64_t primary_key() const { return id; }
    };
    using balances_table = multi_index<"balances"_n, balances>;
};
