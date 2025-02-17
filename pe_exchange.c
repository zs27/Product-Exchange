#include "pe_exchange.h"
#include <sys/types.h>
#include <signal.h>
#include <math.h>


#define LEN 128

typedef enum { false, true } bool;

typedef struct {
    char* item;
    int quantity;
    int balance;
} Position;


typedef struct {
    int terminated;
    int trader_num;
    pid_t pid;
    int trader_fd;
    int exchange_fd;
    char trader_fifo[LEN];
    char exchange_fifo[LEN];
    int num_positions;
    int balance;
    int quantity;
    int orders_sent;
    Position* position_array;
} Trader;

typedef struct {
    int pos;
    int order_id;
    char* type;
    char* product;
    int quantity;
    int price;
    int trader_num;
} Order;



typedef struct {
    int fees_collected;
} Exchange;

Trader *traders = NULL;
Exchange *exchange = NULL;
Order *orders = NULL;

int num_of_traders;

int num_of_orders = 0;
int current_trader_num = -1;
int number_of_traded;
int order_id = 0;
int num_of_added = 0;
bool sig_usr = false;
int traders_connected = 0;

int min(int a, int b) {
    if (a < b){
        return a;
    } else if (a > b) {
        return b;
    } else {
        return a;
    }
}


void cleanup(int i) {
    // close and remove all FIFOs
    traders[i].terminated = 1;

    close(traders[i].exchange_fd);
    close(traders[i].trader_fd);
    unlink(traders[i].exchange_fifo);
    unlink(traders[i].trader_fifo);

}

void free_orders() {
    for (int i = 0; i < num_of_orders; i++) {
        free(orders[i].type);
        free(orders[i].product);
    }
    free(orders);
}

void signalchildhandler(int sig, siginfo_t *info, void *context) {

    pid_t pid = info->si_pid;
    for (int i = 0; i < num_of_traders; i++){

        if (pid == traders[i].pid) {
            printf("[PEX] Trader %d disconnected\n", traders[i].trader_num);
            cleanup(i);
            traders_connected--;
        }
    }

    if (traders_connected == 0) {
        printf("[PEX] Trading completed\n");
        printf("[PEX] Exchange fees collected: $%d\n", exchange[0].fees_collected);
        if (num_of_orders != 0) {
            free_orders();
        }
        exit(0);
    } else {
        return;
    }
    // clean up all pipes
}

void sigusrsignalhandler(int sig, siginfo_t *info, void *context) {
    
    pid_t pid = info->si_pid;
    for (int i = 0; i < num_of_traders; i++){
        if (pid == traders[i].pid) {
            current_trader_num = traders[i].trader_num;
        }
    }
    sig_usr = true;
}

void write_to_trader(int trader_num, char *message) {
    if (traders[trader_num].terminated != 1){
        if (write(traders[trader_num].exchange_fd, message, strlen(message)) == -1) {
            perror("write failed");
            exit(1);
        }
        kill(traders[trader_num].pid, SIGUSR1);
    }
}


char *str_to_pointer(const char *word) { // makes a pointer
    size_t len = strlen(word) + 1; // +1 for null terminator
    char *return_word = malloc(len);
    if (return_word == NULL) {
        exit(1);
    }
    strcpy(return_word, word); 
    return return_word;
}

char* read_pipe(int trader_num) {
    char buffer[LEN + 1];
    ssize_t num_bytes_read = read(traders[trader_num].trader_fd, buffer, LEN);
    if (num_bytes_read == -1) {
        perror("read failed");
        exit(1);
    }
    buffer[num_bytes_read] = '\0';
    return str_to_pointer(buffer);
}

void add_order(char* line) {
    orders = realloc(orders, (num_of_orders + 1) * sizeof(Order));
    if (orders == NULL) {
        perror("realloc failed");
        exit(1);
    }

    Order new_order;
    memset(&new_order, 0, sizeof(Order));
    new_order.order_id = traders[current_trader_num].orders_sent; // unique for each trader
    new_order.pos = num_of_added;
    new_order.trader_num = current_trader_num;
    char *token;
    token = strtok(line, " ");
    int counter = 0;

    while (token != NULL) {
        if (counter == 0){
            new_order.type = str_to_pointer(token);
        } else if (counter == 1) {
            
        } else if (counter == 2) {
            new_order.product = str_to_pointer(token);
        } else if (counter == 3) {
            new_order.quantity = atoi(token);
        } else if (counter == 4) {
            new_order.price = atoi(token);
        } else {
            // error
        }
        
        token = strtok(NULL, " ");
        counter++;
    }

    orders[num_of_orders] = new_order;
    traders[current_trader_num].orders_sent++;
    num_of_orders++;
    num_of_added++;
    order_id++;
}

void print_orderbook(int number_of_traded, char** products) {

    printf("[PEX]	--ORDERBOOK--\n");
    for (int i = 0; i < number_of_traded; i++) {
        // cycle through to find buy sell orders with the same product name
        int buy_orders = 0;
        int sell_orders = 0;
        int curr_price = 0;
        int last_idx = num_of_orders - 1;
        int num_of_orders_for_product = 0;

        // unique prices
        for (int j = 0; j < num_of_orders; j++) {
            if (curr_price == 0) {
                curr_price = orders[j].price;
            }
            if (strcmp(orders[j].product, products[i]) == 0) {
                num_of_orders_for_product++;
            }

            if (j == last_idx || (j < last_idx && (orders[j].price != orders[j+1].price || strcmp(orders[j].product, orders[j+1].product) != 0))) {
                if (strcmp(orders[j].product, products[i]) == 0) {
                    if (strcmp(orders[j].type, "BUY") == 0) {
                        buy_orders++;
                    } else if (strcmp(orders[j].type, "SELL") == 0) {
                        sell_orders++;
                    }
                }

                if (j < last_idx) {
                    curr_price = orders[j+1].price;
                }
            }
        }
        printf("[PEX]	Product: %s; Buy levels: %d; Sell levels: %d\n", products[i], buy_orders, sell_orders);
        

        Order* product_orders = malloc(num_of_orders_for_product * sizeof(Order));

        int pos = 0;
        for (int j = 0; j < num_of_orders; j++) {
            if (strcmp(orders[j].product, products[i]) == 0) {
                product_orders[pos] = orders[j];
                pos++;
            }
        }

        for (int j = 0; j < num_of_orders_for_product; j++) {
            for (int k = j+1; k < num_of_orders_for_product; k++) {
                if (product_orders[j].price > product_orders[k].price) {
                    Order temp = product_orders[j];
                    product_orders[j] = product_orders[k];
                    product_orders[k] = temp;
                }
            }
        }


        int current_price = -1;
        int current_quantity = 0;
        int num_sell_orders = 0;
        for (int j = num_of_orders_for_product-1; j >= 0; j--) {
            
            if(strcmp(product_orders[j].type, "SELL") == 0){
                if (current_price == -1){
                    current_price = product_orders[j].price;
                }
                if (current_price != product_orders[j].price){
                    //print here
                    if (num_sell_orders > 1){
                        printf("[PEX]		SELL %d @ $%d (%d orders)\n", current_quantity, current_price, num_sell_orders);
                    } else {
                        printf("[PEX]		SELL %d @ $%d (%d order)\n", current_quantity, current_price, num_sell_orders);
                    }
                    //update current price to next one
                    current_quantity = product_orders[j].quantity;
                    current_price = product_orders[j].price;
                    num_sell_orders = 1;
                } else {
                    current_quantity += product_orders[j].quantity;
                    num_sell_orders++;
                    //increment
                }
            }
        }
        //print a final time here
        if (num_sell_orders > 0) {
            if (num_sell_orders > 1){
                printf("[PEX]		SELL %d @ $%d (%d orders)\n", current_quantity, current_price, num_sell_orders);
            } else {
                printf("[PEX]		SELL %d @ $%d (%d order)\n", current_quantity, current_price, num_sell_orders);
            }
        }


        current_price = -1;
        current_quantity = 0;
        int num_buy_orders = 0;
        for (int j = num_of_orders_for_product-1; j >= 0; j--) {
            if(strcmp(product_orders[j].type, "BUY") == 0){
                if (current_price == -1){
                    current_price = product_orders[j].price;
                }
                if (current_price != product_orders[j].price){
                    //print here
                    if (num_buy_orders > 1){
                        printf("[PEX]		BUY %d @ $%d (%d orders)\n", current_quantity, current_price, num_buy_orders);
                    } else {
                        printf("[PEX]		BUY %d @ $%d (%d order)\n", current_quantity, current_price, num_buy_orders);
                    }
                    //update current price to next one
                    current_quantity = product_orders[j].quantity;
                    current_price = product_orders[j].price;
                    num_buy_orders = 1;
                } else {
                    current_quantity += product_orders[j].quantity;
                    num_buy_orders++;
                    //increment
                }
            }
        }

        if (num_buy_orders > 0) {
            if (num_buy_orders > 1){
                printf("[PEX]		BUY %d @ $%d (%d orders)\n", current_quantity, current_price, num_buy_orders);
            } else {
                printf("[PEX]		BUY %d @ $%d (%d order)\n", current_quantity, current_price, num_buy_orders);
            }
        }
        free(product_orders);
    }
    printf("[PEX]	--POSITIONS--\n");
    for (int i = 0; i < num_of_traders; i++) {
        printf("[PEX]	Trader %d:", i);
        for (int j = 0; j < number_of_traded; j++) {
            if (j == number_of_traded - 1){
                printf(" %s %d ($%d)\n", traders[i].position_array[j].item, traders[i].position_array[j].quantity, traders[i].position_array[j].balance);
            } else {
                printf(" %s %d ($%d),", traders[i].position_array[j].item, traders[i].position_array[j].quantity, traders[i].position_array[j].balance);
            }
        }
    }
}

void remove_order(int index) {
    if (index < 0 || index >= num_of_orders) {
        return;
    }

    free(orders[index].product);
    free(orders[index].type);

    num_of_orders--;

    for (int i = index + 1; i <= num_of_orders; i++) {
        orders[i-1] = orders[i];
    }


    orders = realloc(orders, num_of_orders * sizeof(Order));
    if (orders == NULL) {
        perror("realloc failed");
        exit(1);
    }

}

void match_orders(){	

    for(int i = 0; i < num_of_orders; i++) {
        for(int j = i+1; j < num_of_orders; j++) {
            if(orders[i].price > orders[j].price) {
                // Swap orders[i] and orders[j]
                struct Order* temp = malloc(sizeof(Order));
                memcpy(temp, &orders[i], sizeof(Order));
                memcpy(&orders[i], &orders[j], sizeof(Order));
                memcpy(&orders[j], temp, sizeof(Order));
                free(temp);
            }
            else if (orders[i].price == orders[j].price && orders[i].pos < orders[j].pos) {
                // Swap orders[i] and orders[j]
                struct Order* temp = malloc(sizeof(Order));
                memcpy(temp, &orders[i], sizeof(Order));
                memcpy(&orders[i], &orders[j], sizeof(Order));
                memcpy(&orders[j], temp, sizeof(Order));
                free(temp);
            }
        }
    }


    int curr_price_sell = 0;
    int curr_price_buy = 0;
    int quantity = 0;


    for (int i = 0; i < num_of_orders; i++){
    
        curr_price_sell = 0;
        curr_price_buy = 0;
        quantity = 0;
        for (int k = 0; k < num_of_orders; k++) {

            if(strcmp(orders[k].product, orders[i].product) == 0){
                if(strcmp(orders[k].type, "SELL") == 0){
                    curr_price_sell = orders[k].price;
                    for (int l = num_of_orders - 1; l >= 0; l--) {
                        if(strcmp(orders[l].product, orders[i].product) == 0){
                            if(strcmp(orders[l].type, "BUY") == 0){
                                curr_price_buy = orders[l].price;

                                if (curr_price_buy < curr_price_sell && orders[k].trader_num != orders[l].trader_num) { 
                                    return;
                                } else {

                                    quantity = min(orders[l].quantity, orders[k].quantity);

                                    orders[l].quantity -= quantity;
                                    orders[k].quantity -= quantity;

                                    traders[orders[l].trader_num].position_array[i].quantity += quantity;
                                    traders[orders[k].trader_num].position_array[i].quantity -= quantity;


                                    if (orders[k].pos < orders[l].pos) { // seller lower

                                        int transaction_cost = quantity * orders[k].price;


                                        int commission = round(transaction_cost * 0.01);

                                        traders[orders[k].trader_num].position_array[i].balance += (transaction_cost);
                                        traders[orders[l].trader_num].position_array[i].balance -= (transaction_cost + commission);
                                        exchange[0].fees_collected += commission;

                                        printf("[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", orders[k].order_id ,orders[k].trader_num, orders[l].order_id, orders[l].trader_num ,transaction_cost,commission); // newer on right
                                    } else { // buyer lower 
                                        
                                        int transaction_cost = quantity * orders[l].price;

                                        int commission = round(transaction_cost * 0.01);
                                        exchange[0].fees_collected += commission;

                                        traders[orders[k].trader_num].position_array[i].balance += (transaction_cost - commission);
                                        traders[orders[l].trader_num].position_array[i].balance -= (transaction_cost);
                                        printf("[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", orders[l].order_id ,orders[l].trader_num, orders[k].order_id, orders[k].trader_num ,transaction_cost,commission); // newer on right
                                    }

                                    
                                    char fill_message[LEN];
                                    sprintf(fill_message, "FILL %d %d;", orders[l].order_id, quantity);
                                    
                                    
                                    write_to_trader(orders[l].trader_num, fill_message); 

                                    char fill_message2[LEN];
                                    sprintf(fill_message2, "FILL %d %d;", orders[k].order_id, quantity);
                                    write_to_trader(orders[k].trader_num, fill_message2);
                                    
                                    int temp_k = k;
                                    if (orders[l].quantity == 0) {
                                        remove_order(l);
                                        if(temp_k != 0){
                                            temp_k = temp_k-1;
                                        }
                                    }
                                    
                                    if (orders[temp_k].quantity == 0) {
                                        remove_order(temp_k);
                                        break;
                                    }

                                }
                            }
                        }
                    }
                }
            }
        } 
    }

}

int main(int argc, char **argv) {
    printf("[PEX] Starting\n");

    if (argc < 2){
        perror("not enough arguments");
        exit(1);
    }

    char *trading_file = argv[1];
    FILE *tf = fopen(trading_file, "r");
    if (tf == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    int counter = 0;
    char** products;

    char line[LEN];

    while (fgets(line, LEN, tf) != NULL) {
        // do something with the line
        if (counter == 0) {
            number_of_traded = atoi(line);
            products = malloc(number_of_traded * sizeof(char *));
            if (products == NULL) {
                // if malloc fails
                perror("malloc failed");
                exit(1);
            }
        } else {
            if (counter > number_of_traded) {
                perror("file formatted incorrectly");
                exit(1);
            }
            line[strcspn(line, "\n")] = '\0';
            products[counter-1] = str_to_pointer(line);
            if (products[counter-1] == NULL){
                perror("line saving failed");
                exit(1);
            }
        }
        counter++;
    }

    fclose(tf);

    

    printf("[PEX] Trading %d products:", number_of_traded);
    for (int i = 0; i < number_of_traded; i++) {
        printf(" %s", products[i]);
    }
    printf("\n");


    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_sigaction = signalchildhandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigusrsignalhandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    num_of_traders = argc - 2;

    traders = realloc(traders, num_of_traders * sizeof(Trader));
    exchange = realloc(exchange, 1 * sizeof(Exchange));
    
    Exchange exchange_F;
    exchange_F.fees_collected = 0;
    exchange[0] = exchange_F;

    if (traders == NULL){
        //malloc didnt work
        perror("trader malloc didnt work");
        return 1;
    }


    for (int trader_num = 0; trader_num < num_of_traders; trader_num++) {
        Trader trader;
        trader.trader_num = trader_num;
        trader.orders_sent = 0;
        trader.terminated = 0;

        // add positions to the trader as 0
        trader.num_positions = number_of_traded;  // for example, allocate space for 10 positions
        trader.position_array = malloc(trader.num_positions * sizeof(Position));
        memset(trader.position_array, 0, trader.num_positions * sizeof(Position));

        for (int j = 0; j < number_of_traded; j++) {
            trader.position_array[j].item = str_to_pointer(products[j]);
            trader.position_array[j].quantity = 0;
            trader.position_array[j].balance = 0;
        }

        char exchange_fifo_name[LEN];
        sprintf(exchange_fifo_name, FIFO_EXCHANGE, trader_num);
        if (mkfifo(exchange_fifo_name, 0666) == -1) {
            perror("mkfifo failed");
            exit(1);
        }
        printf("[PEX] Created FIFO %s\n", exchange_fifo_name);
        strcpy(trader.exchange_fifo, exchange_fifo_name);

        char trader_fifo_name[LEN];
        sprintf(trader_fifo_name, FIFO_TRADER, trader_num);
        if (mkfifo(trader_fifo_name, 0666) == -1) {
            perror("mkfifo failed");
            exit(1);
        }
        printf("[PEX] Created FIFO %s\n", trader_fifo_name);
        strcpy(trader.trader_fifo, trader_fifo_name);

        traders[trader_num] = trader;

        traders[trader_num].pid = fork();

        if (traders[trader_num].pid == -1) {
            perror("fork failed");
            exit(1);
        }

        if (traders[trader_num].pid == 0) {
            // Child process
            char trader_id_str[12];
            sprintf(trader_id_str, "%d", trader_num);

            printf("[PEX] Starting trader %d (%s)\n", trader_num, argv[trader_num+2]);
            execl(argv[trader_num+2], trader_id_str);

            perror("exec failed");
            exit(1);
        } else {

            traders[trader_num].exchange_fd = open(exchange_fifo_name, O_WRONLY);
            
            if (traders[trader_num].exchange_fd == -1) {
                perror("failed to open exchange pipe");
                exit(1);
            }
            printf("[PEX] Connected to %s\n", exchange_fifo_name);

            traders[trader_num].trader_fd = open(trader_fifo_name, O_RDONLY);
        
            if (traders[trader_num].trader_fd == -1) {
                perror("failed to open trader pipe");
                exit(1);
            }
            printf("[PEX] Connected to %s\n", trader_fifo_name);
        }
        traders_connected++;

    }

    // send opening message to all traders
    for (int i = 0; i < num_of_traders; i++){
        write_to_trader(i, "MARKET OPEN;");
    }

    while (true) {
        // action loop 
        sig_usr = false;
        pause();
        if (!sig_usr) {
            continue;
        }

        bool send = false;
        char* message = read_pipe(current_trader_num);
        size_t len = strlen(message);
        char * return_message = str_to_pointer(message);

        int price;
        int order_id;
        char product[100];
        int qty;
        char new_message[LEN];
        if (sscanf(return_message, "BUY %d %s %d %d", &order_id, product, &qty, &price) == 4) {
            snprintf(new_message, sizeof(new_message), "MARKET BUY %s %d %d;", product, qty, price);
        } else if (sscanf(return_message, "SELL %d %s %d %d", &order_id, product, &qty, &price) == 4) { 
            snprintf(new_message, sizeof(new_message), "MARKET SELL %s %d %d;", product, qty, price);
        }


        if (len > 0 && message[len-1] == ';') {
            message[len-1] = '\0';
        }
        printf("[PEX] [T%d] Parsing command: <%s>\n", current_trader_num,message);
        
        if (strncmp(message, "BUY", 3) == 0) {
            send = true;

            add_order(message);

            char *response;
            asprintf(&response, "ACCEPTED %d;", traders[current_trader_num].orders_sent-1); // asprint f allocates memory
            write_to_trader(current_trader_num, response);
            free(response);
            
        } else if (strncmp(message, "SELL", 4) == 0) {
            send = true;
            add_order(message);
            char *response;
            asprintf(&response, "ACCEPTED %d;", traders[current_trader_num].orders_sent-1); // asprint f allocates memory
            write_to_trader(current_trader_num, response);
            free(response);

        } else {
            printf("incorrect message: %s\n", message);
            continue;
            // handle error
        }
        free(message);
            //send back the message
        if (send == true){
            for (int i = 0; i < num_of_traders; i++){
                if (i == current_trader_num){
                    continue;
                }
                write_to_trader(i, new_message);
            }	
        }
        match_orders();
        free(return_message);
        print_orderbook(number_of_traded, products);

    }

    for (int i = 0; i < number_of_traded; i++) {
        free(products[i]);
    }
    free(products);

    for (int i = 0; i < num_of_traders; i++) {
        for (int j = 0; j < number_of_traded; j++) {
            free(traders[i].position_array[j].item);
        }
        free(traders[i].position_array);
    }

    free(traders);

    return 0;
}