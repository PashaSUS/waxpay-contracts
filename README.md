# WaxPay Smart Contracts

WaxPay is a modular on-chain payment system designed for projects and merchants building on the WAX blockchain. It offers a complete solution for managing accepted tokens, handling payments, setting store-specific rules, and distributing revenue among recipients.

## Overview

This repository contains three interconnected EOSIO-based smart contracts:

1. `` – Global token whitelist and metadata.
2. `` – Store whitelist and store-specific token configuration.
3. `` – Payment processing and revenue distribution logic.

---

## Contract: `twl.waxpayio`

**Token Whitelist Contract**

Manages globally accepted tokens including their metadata, system fee, and default slippage.

### Actions

- `addtoken(contract, symbol, image_link, system_fee)` – Whitelist a new token.
- `changesysfee(id, system_fee)` – Update system fee for a token.
- `changeimage(id, image_link)` – Update the image URL for a token.
- `addslippage(id, slippage)` – Update slippage for a token.
- `rmtoken(id)` – Remove a token from the whitelist (also removes from all stores).
- `cls()` – Clear all tokens (admin only).

---

## Contract: `swl.waxpayio`

**Store Whitelist Contract**

Stores register under this contract, configure their accepted tokens, and define how payments are split among recipients.

### Store Management

- `addstore(store_id, store_name, authenticated_account)` – Register a new store.

### Recipient Management

- `addrecipient(user, recipient, weight)` – Add a recipient with a share weight.
- `rmvrec(user, recipient)` – Remove one recipient.
- `rmvrecs(user)` – Remove all recipients.

### Token Support Per Store

- `addtoken(user, id, min_slippage, max_slippage, usd_value)` – Add a supported token to a store.
- `edittoken(user, id, min_slippage, max_slippage, usd_value)` – Modify a store token.
- `changestate(user, id, active)` – Toggle token status.
- `rmvtoken(user, id)` – Remove a token from a store.
- `rmvsystoken(id)` – Admin-only removal of a token from all stores.

### Utilities

- `cls()` – Admin action to clear all stores and related data.

---

## Contract: `waxpayio`

**Main Payment Processor**

Handles incoming transfers, order creation, acceptance, fund distribution, and claims.

### On-Transfer Handler

- `orderpaid(from, to, quantity, memo)` – Automatically called when a payment is sent to the contract. It records the order.

### Actions

- `auth(user, auth_code)` – Dummy action for off-chain validation/auth.
- `acceptorder(system_id, store_id, memo)` – Accept an order, charge system fees, and distribute remaining funds to recipients.
- `rejectorder(system_id)` – Reject an order and queue a refund.
- `claim(user)` – Users can claim their rejected funds.
- `cls()` – Admin-only function to clear all pending orders.

### Internal Logic

- `check_token(...)` – Validates a token against the whitelist.
- `deny_order(...)` – Moves rejected tokens into an internal balance.
- `send_tokens(...)` – Utility for performing inline EOSIO token transfers.

---

## System Accounts

| Contract       | Description                            |
| -------------- | -------------------------------------- |
| `twl.waxpayio` | Global token whitelist                 |
| `swl.waxpayio` | Store registry and per-store settings  |
| `waxpayio`     | Main payment logic and distribution    |
| `fee.waxpayio` | Receives system fees and rounding dust |

---

## Design Principles

- **Security**: All sensitive operations are protected via `require_auth` and scoped to system contracts.
- **Modularity**: Global and store-level configurations are separated for maximum flexibility.
- **Extensibility**: Support for adding/removing tokens, stores, recipients, and slippage configurations.
- **Transparency**: All distribution logic is recorded and auditable on-chain.

---

## Example Flow

1. Admin whitelists a token via `twl.waxpayio::addtoken`.
2. A store is added via `swl.waxpayio::addstore`.
3. Store associates recipients and supported tokens.
4. User sends tokens to `waxpayio` with a unique memo.
5. Admin/store owner calls `acceptorder`.
6. Funds are split based on system fee and store rules.
7. Rejected orders can be refunded via `claim()`.

---

## Build & Deploy

Use the EOSIO CDT to compile each contract and deploy them to their respective accounts:

```sh
cd contracts/<contract>
eosio-cpp -abigen -o <contract>.wasm <contract>.cpp
cleos set contract <account> <contract_dir> -p <account>@active
```

---

## License

MIT License. Open source and free to use for WAX community and builders.

---
