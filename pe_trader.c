#include "pe_trader.h"

#define BUFFERSIZE 128
#define NUM_OF_WORDS 5

typedef enum { false, true } bool;

void signalHandler(int sig){
}

char* parse_order(char buffer[], int order_num) {
    char* word = strtok(buffer, " ");
    char* returnstr = malloc(BUFFERSIZE);
    snprintf(returnstr, BUFFERSIZE, "BUY %d", order_num);

    int iterations = 0;
    while (word != NULL) {
        if (iterations >= NUM_OF_WORDS){
            perror("Incorrectly formatted pipe message");
            free(returnstr);
            return NULL;
        }
        if (iterations <= 4 && iterations >= 2){
            if (iterations == 3){
                if (atoi(word) >= 1000){
                    free(returnstr);
                    return NULL;
                }
            }
            strcat(returnstr, " ");
            strcat(returnstr, word);
        }

        word = strtok(NULL, " ");
        iterations++;
    }

    return returnstr;
}

int main(int argc, char ** argv) {
    pid_t process_of_parent = getppid();
    int next_order_id = 0;

    if (argc < 2) {
        perror("Not enough arguments");
        return 1;
    }

    // register signal handler
    int trader_id;
    if (sscanf(argv[1], "%d", &trader_id) != 1){
        perror("id input invalid");
        return 1;
    }


    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = &signalHandler;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    
    // connect to named pipes

    char exchange_fifo[BUFFERSIZE];
    snprintf(exchange_fifo, BUFFERSIZE, FIFO_EXCHANGE, trader_id);

    int read_from_exchange = open(exchange_fifo, O_RDONLY);
    if (read_from_exchange == -1) {
        perror("failed to open pipe");
    }

    char trader_fifo[BUFFERSIZE];
    snprintf(trader_fifo, BUFFERSIZE, FIFO_TRADER, trader_id);

    int write_to_exchange = open(trader_fifo, O_WRONLY);
    if (write_to_exchange == -1) {
        perror("failed to open pipe");
    }

    // event loop:
    char buffer[BUFFERSIZE];

    while (true){

        
        pause();
        ssize_t bytes_read = read(read_from_exchange, buffer, sizeof(buffer));

        if (bytes_read == -1) {
            break;
        }
        buffer[bytes_read] = '\0';

        if (strstr(buffer, "MARKET SELL") == NULL) {
            continue;
        } 

        char* final_message = parse_order(buffer, next_order_id);
        if (final_message == NULL){
            break;
        }
        char str[BUFFERSIZE];
        strcpy(str, final_message);
        int buffer_len = strlen(str);

        for (int i = 0; i < buffer_len; i++) {
            ssize_t output = write(write_to_exchange, &str[i], 1);
            if (output == -1) {
                perror("write error");
                break;
            }
        }
        kill(process_of_parent, SIGUSR1); 
        free(final_message);
        next_order_id++;
    }
    close(read_from_exchange);
    close(write_to_exchange);
    return 0;
}