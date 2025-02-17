# Product-Exchange

This project implements a Local exchange and trader in C, the exchange reads product data, counts the products, and then sets up connections to the traders. It launches trader processes and communicates with them via pipes to execute orders and send market updates. The exchange is then matching buy and sell orders based on current active positions and logging the activity in an orderbook.

Written for comp2017 at usyd
