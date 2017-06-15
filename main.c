#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>
#include "WinSock2.h"
#include <openssl\ssl.h>
#include <openssl\hmac.h>
#include <Windows.h>
#include <math.h>

#pragma comment(lib,"ws2_32.lib")

// stack handles
typedef enum {
	TREND_STATE_UNDEFINED,
	TREND_STATE_STAGNANT,
	TREND_STATE_INCREASING,
	TREND_STATE_DECREASING,
	TREND_STATE_OSCILLATORY
} trendState;

typedef enum {
	ORDER_STATE_ACTIVE,
	ORDER_STATE_EMPTY,
} orderState;

typedef struct {
	trendState trend;
	orderState order;
	time_t orderTimer;
	time_t probeTimer;
	time_t trendTimer;
	time_t anchorTimer;
	char name[5];
	float num;
	float askSat;
	float bidSat;
	float spread;
	char btcValue[11];
	float btcNet;
	float anchor[2];
	int deviation;
	char sellUUID[37];
	char buyUUID[37];
	float dMin;
	float dHr;
	float dTwelveHr;
	float dDay;
	float dThreeDay;
	float dWeek;
	float dMonth;
	float dThreeMonth;
} coin;

typedef struct {
	int transactions;
	int mistakes;
	float tPerHour;
	float margin;
	time_t timer;
} botData;

struct {
	WSADATA wsa;
	SOCKET s;
	struct sockaddr_in bittrex;
	SSL* conn;
} socketshit;
SSL_CTX* ctx;

// create a "self" struct for all of this at some point
unsigned char key[] = { 0x39, 0x34, 0x39, 0x39, 0x63, 0x65, 0x36, 0x37, 0x66, 0x34, 0x31, 0x66, 0x34, 0x34, 0x64, 0x62, 0x62, 0x64, 0x30, 0x36, 0x31, 0x65, 0x31, 0x36, 0x38, 0x37, 0x31, 0x63, 0x37, 0x64, 0x37, 0x64 };
char* secret = "9499ce67f41f44dbbd061e16871c7d7d";
float btcAsset;
int btcAssetSat;

// prototypes
int connectBittrex();
void disconnectBittrex();
void genSignature(char* message, char* hash);
void initCoins(coin* coins, int numCoins);
int buyCoins(coin* coin, int rate);
int sellCoins(coin* coin, int rate);
int getCoinBalance(coin* coin);
int getCoinPrice(coin* coin);
int getOrder(char* uuid);
int cancelOrder(char* uuid);

int connectBittrex()
{
	// returns 1 on success, 0 on failure

	// init SSL
	socketshit.bittrex.sin_addr.s_addr = inet_addr("104.17.156.108");
	socketshit.bittrex.sin_family = AF_INET;
	socketshit.bittrex.sin_port = htons(443);

	// interface initialize
	if (WSAStartup(MAKEWORD(2, 2), &socketshit.wsa))
	{
		printf("Failed to initialize winsock: %s\n", WSAGetLastError());
		return 0;
	}

	// fill socket
	if ((socketshit.s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		printf("Could not create socket: %s\n", WSAGetLastError());
		return 0;
	}
	// connect to bittrex
	if (connect(socketshit.s, (struct socckaddr *)&socketshit.bittrex, sizeof(socketshit.bittrex)) < 0)
	{
		printf("Looking for network...\n");
		while (connect(socketshit.s, (struct socckaddr *)&socketshit.bittrex, sizeof(socketshit.bittrex)) < 0)
		{
			Sleep(1000);
		}
	}

	// init SSL
	SSL_load_error_strings();
	SSL_library_init();
	ctx = SSL_CTX_new(SSLv23_client_method());

	socketshit.conn = SSL_new(ctx);
	SSL_set_fd(socketshit.conn, socketshit.s);

	if (SSL_connect(socketshit.conn) != 1)
	{
		printf("SSL failed to connect.\n");
		return 0;
	}

	return 1;
}

void disconnectBittrex()
{
	closesocket(socketshit.s);
	WSACleanup();
}

void genSignature(char* message, char* hash)
{
	// HERE BE MADNESS

	int len;
	char buffer[8];

	unsigned char* result = HMAC(EVP_sha512(), key, (int)sizeof(key), (unsigned char*)message, strlen(message), NULL, &len);

	// digest hex
	sprintf(&buffer[0], "%02x", (unsigned char)result[0]);
	strcpy(hash, buffer);
	for (int i = 1; i < len; i++)
	{
		sprintf(&buffer[0], "%02x", (unsigned char)result[i]);
		strcat(hash, buffer);
	}
}

void initCoins(coin* coins, int numCoins)
{
	// siphon all data from drive ultimately, though, manual assignment for now
	int i;

	// DGB
	strcpy(coins[0].name, "ARK");
	// ARK
	strcpy(coins[1].name, "DGB");


	printf("Retrieving account balances...\n");
	for (i = 0; i < numCoins; i++)
	{
		// get account balance
		getCoinBalance(&coins[i]);

		// initialize anchors
		coins[i].anchor[0] = 0;
		coins[i].anchor[1] = 0;

		coins[i].trend = TREND_STATE_UNDEFINED;
		coins[i].order = ORDER_STATE_EMPTY;

		time(&coins[i].trendTimer);
		time(&coins[i].anchorTimer);
	}
}

int getCoinBalance(coin* coin)
{
	// returns 1 on success, 0 on failure

	char message[1000];
	char reply[5000];
	char hash[5000];
	char buff[5000];
	char btcSatBuff[32];
	char nonce[50];
	int received;
	int j, k;

	char* balance = malloc(sizeof(char) * 32);
	// build header
	strcpy(message, "GET ");
	strcpy(buff, "https://bittrex.com/api/v1.1/account/getbalance?apikey=d8dafd208fdd4233ac5c522cc65299d4&currency=");
	strcat(buff, coin->name);
	strcat(buff, "&nonce=");
	sprintf(nonce, "%i", time(NULL));
	strcat(buff, nonce);
	strcat(message, buff);
	strcat(message, " HTTP/1.1\r\nHost: bittrex.com\r\napisign:");
	genSignature(buff, hash);
	strcat(message, hash);
	strcat(message, "\r\n\r\n");

	if (SSL_write(socketshit.conn, message, strlen(message)) <= 0)
	{
		printf("Failed to acquire balance of %s; could not send message to server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	if ((received = SSL_read(socketshit.conn, reply, 5000)) <= 0)
	{
		printf("Failed to acquire balance of %s; could not receive message from server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}

	// balance starts at 583 + strlen(coin[i].name) of reply
	j = 583 + strlen(coin->name);
	k = 0;
	// BTC specific
	if (coin->name[0] == 'B' && coin->name[1] == 'T')
	{
		while (j)
		{
			if (reply[j] == '.')
			{
				j++;
				continue;
			}
			if (isdigit(reply[j]))
			{
				btcSatBuff[k] = reply[j];
				j++;
				k++;
				continue;
			}
			break;
		}
	}
	btcAssetSat = atoi(btcSatBuff);

	j = 583 + strlen(coin->name);
	k = 0;
	while (j)
	{
		if (isdigit((int)reply[j]))
		{
			balance[k] = reply[j];
			j++;
			k++;
			continue;
		}
		if (reply[j] == '.')
		{
			balance[k] = reply[j];
			j++;
			k++;
			continue;
		}
		break;
	}
	coin->num = (float)atof(balance);

	printf("Total %s: %f\n", coin->name, coin->num);
	free(balance);
	balance = NULL;

	// clear socket buffer
	SSL_read(socketshit.conn, reply, 5000);

	return 1;
}

int buyCoins(coin* coin, int rate)
{
	// returns 1 on success, 0 on failure
	// rate should be in sat form

	char message[1000];
	char reply[5000];
	char buffer[512];
	char buffer2[512];
	char buffer3[512];
	char btcRate[] = { '0', '.', '0', '0',  '0',  '0',  '0',  '0',  '0',  '0', '\0' };
	char success[512];
	char hash[512];
	char nonce[50];
	int received;

	// convert sat to btc
	sprintf(buffer3, "%i", rate);
	memcpy(&btcRate[10 - strlen(buffer3)], buffer3, strlen(buffer3));

	// build header
	strcpy(message, "GET ");
	strcpy(buffer, "https://bittrex.com/api/v1.1/market/buylimit?apikey=d8dafd208fdd4233ac5c522cc65299d4&market=btc-");
	strcat(buffer, coin->name);
	strcat(buffer, "&quantity=");
	sprintf(buffer2, "%f", coin->num * 1.0050); // quantity
	strcat(buffer, buffer2);
	strcat(buffer, "&rate=");
	strcat(buffer, btcRate);
	strcat(buffer, "&nonce=");
	sprintf(nonce, "%i", time(NULL)); // time stamp of order
	strcat(buffer, nonce);
	genSignature(buffer, hash);
	strcat(message, buffer);
	strcat(message, " HTTP/1.1\r\nHost: bittrex.com\r\n");

	// append signature
	strcat(message, "apisign:");
	strcat(message, hash);
	strcat(message, "\r\n\r\n");

	// send and receive
	if (SSL_write(socketshit.conn, message, strlen(message)) <= 0)
	{
		printf("Failed to buy %s; could not send message to server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	received = SSL_read(socketshit.conn, reply, 5000);
	if (received <= 0)
	{
		printf("Failed to buy %s; could not receive message from server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}	
	reply[received] = '\0';

	// check for market success
	memcpy(success, strstr(reply, "success"), 80);
	if (success[9] == 'f')
	{
		printf("Failed to buy %s: %s\n", coin->name, success);
		SSL_read(socketshit.conn, reply, 5000); // clear socket buffer
		return 0;
	}
	
	// acquire uuid
	memcpy(&coin->buyUUID[0], strstr(&reply[0], "uuid") + 7, 36);

	printf("Buying %f %s at %i sat/ea.\n", coin->num * 1.0050, coin->name, rate);

	SSL_read(socketshit.conn, reply, 5000); // clear socket buffer

	return 1;
}

int sellCoins(coin* coin, int rate)
{
	// returns 1 on success, 0 on failure

	char message[1000];
	char reply[5000];
	char buffer[512];
	char buffer2[512];
	char buffer3[512];
	char btcRate[] = { '0', '.', '0', '0',  '0',  '0',  '0',  '0',  '0',  '0', '\0' };
	char success[512];
	char hash[512];
	char nonce[50];
	int received, j;

	// sat to btc converstion
	sprintf(buffer3, "%i", rate);
	memcpy(&btcRate[10 - strlen(buffer3)], buffer3, strlen(buffer3));

	// build header
	strcpy(message, "GET ");
	strcpy(buffer, "https://bittrex.com/api/v1.1/market/selllimit?apikey=d8dafd208fdd4233ac5c522cc65299d4&market=btc-");
	strcat(buffer, coin->name);
	strcat(buffer, "&quantity=");
	sprintf(buffer2, "%f", coin->num); // quantity
	strcat(buffer, buffer2);
	strcat(buffer, "&rate=");
	strcat(buffer, btcRate); // rate
	strcat(buffer, "&nonce=");
	sprintf(nonce, "%i", time(NULL)); // time stamp of order
	strcat(buffer, nonce);
	genSignature(buffer, hash);
	strcat(message, buffer);
	strcat(message, " HTTP/1.1\r\nHost: bittrex.com\r\n");

	// append signature
	strcat(message, "apisign:");
	strcat(message, hash);
	strcat(message, "\r\n\r\n");

	// send and receive
	if (SSL_write(socketshit.conn, message, strlen(message)) <= 0)
	{
		printf("Failed to sell %s; could not send message to server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	received = SSL_read(socketshit.conn, reply, 5000);
	if (received <= 0)
	{
		printf("Failed to sell %s; could not receive message from server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	reply[received] = '\0';

	// check for market success
	memcpy(success, strstr(reply, "success"), 80);
	if (success[9] == 'f')
	{
		printf("Failed to sell %s: %s\n", coin->name, success);
		SSL_read(socketshit.conn, reply, 5000); // clear socket buffer
		return 0;
	}

	// acquire uuid
	memcpy(&coin->sellUUID[0], strstr(reply, "uuid") + 7, 36);

	printf("Selling %f %s at %i sat/ea.\n", coin->num, coin->name, rate);

	SSL_read(socketshit.conn, reply, 5000); // clear socket buffer

	return 1;
}

int getCoinPrice(coin* coin)
{
	// returns 1 on success, 0 on failure

	char message[1000];
	char reply[5000];
	char askBuff[32];
	char bidBuff[32];
	int received;

	strcpy(message, "GET https://bittrex.com/api/v1.1/public/getmarketsummary?market=btc-");
	strcat(message, coin->name);
	strcat(message, " HTTP/1.1\r\nHost: bittrex.com\r\n\r\n");

	// send and receive
	if (SSL_write(socketshit.conn, message, strlen(message)) <= 0)
	{
		//printf("Failed to acquire %s market data; could not send message to server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	if (received = SSL_read(socketshit.conn, reply, 5000) <= 0)
	{
		//printf("Failed to acquire %s market data; could not receive message from server.\n", coin->name);
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	// clear socket buffer
	SSL_read(socketshit.conn, reply, 5000);

	// search values
	if (strstr(&reply[0], "Ask"))
	{
		strncpy(askBuff, strstr(&reply[0], "Ask") + 8, 7);
		strncpy(bidBuff, strstr(&reply[0], "Bid") + 8, 7);
	}
	askBuff[10] = '\0';
	bidBuff[10] = '\0';

	// assign new market values
	coin->askSat = atoi(askBuff);
	coin->bidSat = atoi(bidBuff);
	coin->spread = coin->askSat - coin->bidSat;

	return 1;
}

int getOrder(char* uuid)
{
	// returns 1 if closed, 0 if open, -1 if failure
	char message[1000];
	char reply[5000];
	char order[1000];
	char hash[1000];
	char buffer[1000];
	char nonce[512];
	int received;

	// build header
	strcpy(message, "GET ");
	strcpy(buffer, "https://bittrex.com/api/v1.1/account/getorder?apikey=d8dafd208fdd4233ac5c522cc65299d4&uuid=");
	strcat(buffer, uuid);
	strcat(buffer, "&nonce=");
	sprintf(nonce, "%i", time(NULL)); // time stamp of order
	strcat(buffer, nonce);
	genSignature(buffer, hash);
	strcat(message, buffer);
	strcat(message, " HTTP/1.1\r\nHost: bittrex.com\r\n");

	// append signature
	strcat(message, "apisign:");
	strcat(message, hash);
	strcat(message, "\r\n\r\n");

	// send and receive
	if (SSL_write(socketshit.conn, message, strlen(message)) <= 0)
	{
		printf("Failed to acquire order information; could not send message to server.\n");
		disconnectBittrex();
		connectBittrex();
		return -1;
	}
	received = SSL_read(socketshit.conn, reply, 5000);
	if (received <= 0)
	{
		printf("Failed to acquire order information; could not receive message from server.\n");
		disconnectBittrex();
		connectBittrex();
		return -1;
	}
	reply[received] = '\0';
	printf("%s\n", reply);
	// get state
	memcpy(order, strstr(reply, "IsOpen"), 80);
	// clear socket buffer
	SSL_read(socketshit.conn, reply, 5000);

	if (order[8] == 't')
	{
		return 0;
	}
	if (order[8] == 'f')
	{
		return 1;
	}
}

int cancelOrder(char* uuid)
{
	char message[1000];
	char buffer[1000];
	char reply[5000];
	char hash[1000];
	char order[1000];
	char nonce[50];
	int received;

	// build header
	strcpy(message, "GET ");
	strcpy(buffer, "https://bittrex.com/api/v1.1/market/cancel?apikey=d8dafd208fdd4233ac5c522cc65299d4&uuid=");
	strcat(buffer, uuid);
	strcat(buffer, "&nonce=");
	sprintf(nonce, "%i", time(NULL));
	strcat(buffer, nonce);
	genSignature(buffer, hash);
	strcat(message, buffer);
	strcat(message, " HTTP/1.1\r\nHost: bittrex.com\r\n");

	// append signature
	//strcat(message, "apisign:");
	//strcat(message, hash);
	//strcat(message, "\r\n\r\n");

	// send and receive
	if (SSL_write(socketshit.conn, message, strlen(message)) <= 0)
	{
		printf("Failed to cancel order; could not send message to server.\n");
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	received = SSL_read(socketshit.conn, reply, 5000);
	if (received <= 0)
	{
		printf("Failed to cancel order; could not receive message from server.\n");
		disconnectBittrex();
		connectBittrex();
		return 0;
	}
	reply[received] = '\0';
	printf("reply: %s\n", reply);
	// clear socket buffer
	SSL_read(socketshit.conn, reply, 5000);
	return 0;
	memcpy(order, strstr(reply, "success"), 100);

	// check for success
	if (order[9] == 'f')
	{
		printf("The order could not be canceled: %s\n", order);
		SSL_read(socketshit.conn, reply, 5000); // clear socket buffer
		return 0;
	}

	printf("%s was successfully canceled.\n", uuid);
	
	// clear socket buffer
	SSL_read(socketshit.conn, reply, 5000);

	return 1;
}

int main(void)
{
	if (!connectBittrex())
	{
		getchar();
		return 0;
	}

	printf("Giving life to bot...\n");
	botData bot;
	bot.margin = (float).0075;
	bot.transactions = 0;
	bot.mistakes = 0;
	Sleep(2000);
	printf("*ominous droning*\n");
	Sleep(2000);
	printf("Greetings, organism.\n");
	Sleep(2000);

	printf("Gathering your coin data...\n");
	Sleep(2000);
	coin btc;
	coin* coins = (coin*)malloc(sizeof(coin) * 2);
	initCoins(coins, 2);
	strcpy(btc.name, "BTC");
	getCoinBalance(&btc);

	time_t t;
	unsigned int i = 1;

	time(&t);
	time(&bot.timer);
	if (btc.num < 1)
	{
		Sleep(2000);
		printf("You appear to be quite fucking poor. Allow me to remedy this situation...\n");
	}
	Sleep(2000);
	printf("Observing market trends...\n");
	while (i)
	{
		for (i = 0; i < 1; i++)
		{
			// acquire new market value
			if (!getCoinPrice(&coins[i]))
			{
				if (!getCoinPrice(&coins[i]))
				{
					printf("Fatal network error.\n");
					continue;
				}
			}

			// generate anchors
			if (coins[i].anchor[0] == 0)
			{
				coins[i].anchor[0] = coins[i].askSat;
				coins[i].anchor[1] = coins[i].anchor[0];
				// calculate deviation
				coins[i].deviation = (int)ceil((.00375 * coins[i].askSat)); // ignore fluctuations < .25%
			}

			// check for new anchor every 10 seconds
			if (difftime(time(NULL), coins[i].anchorTimer) > 10)
			{
				time(&coins[i].anchorTimer);

				if (abs(coins[i].askSat - coins[i].anchor[0]) > coins[i].deviation)
				{
					// new deviation
					coins[i].deviation = (int)ceil((.00375 * coins[i].askSat));

					if (coins[i].askSat > coins[i].anchor[0] && coins[i].anchor[0] > coins[i].anchor[1])
					{
						printf("%s is increasing...\n", coins[i].name);
						coins[i].trend = TREND_STATE_INCREASING;
					}
					else if (coins[i].askSat < coins[i].anchor[0] && coins[i].anchor[0] < coins[i].anchor[1])
					{
						printf("%s is decreasing...\n", coins[i].name);
						coins[i].trend = TREND_STATE_DECREASING;
					}
					else if (coins[i].askSat > coins[i].anchor[0] && coins[i].anchor[0] < coins[i].anchor[1])
					{
						printf("%s is oscillating...\n", coins[i].name);
						coins[i].trend = TREND_STATE_OSCILLATORY;
					}
					else if (coins[i].askSat < coins[i].anchor[0] && coins[i].anchor[0] > coins[i].anchor[1])
					{
						printf("%s is oscillating...\n", coins[i].name);
						coins[i].trend = TREND_STATE_OSCILLATORY;
					}

					// start trend timer
					time(&coins[i].trendTimer);

					// assign new anchors
					coins[i].anchor[1] = coins[i].anchor[0];
					coins[i].anchor[0] = coins[i].askSat;
				}
				// check trend time
				if (difftime(time(NULL), coins[i].trendTimer) > 120)
				{
					printf("%s is stagnant.\n", coins[i].name);
					coins[i].trend = TREND_STATE_STAGNANT;
					time(&coins[i].trendTimer);
				}
				// post new values
				printf("Current %s value: %i Ask, %i Bid, Spread: %i\n", coins[i].name, (int)coins[i].askSat, (int)coins[i].bidSat, (int)coins[i].spread);
			}

			// market algorithms
			switch (coins[i].order)
			{
			case ORDER_STATE_EMPTY:
			{
				switch (coins[i].trend)
				{
				case TREND_STATE_UNDEFINED:
				{
					break;
				}
				/*
				case TREND_STATE_STAGNANT:

				{
					// wait for big spread, clamp
					
					
					if (coins[i].spread > floor(coins[i].askSat * .01))
					{
						// since fluctuations are measured from the ask value, an undetected increase in spread will always be a reduction of the bid
						if (!buyCoins(&coins[i], (int)floor(coins[i].bidSat * 1.001)))
						{
							break;
						}
						if (!sellCoins(&coins[i], (int)ceil(coins[i].askSat * .999)))
						{
							break;
						}
						coins[i].order = ORDER_STATE_ACTIVE;
						time(&coins[i].orderTimer);
						break;
					}
					
					break;
				}
				*/
				/*
				case TREND_STATE_OSCILLATORY:
				{
					
					if (coins[i].spread > ceil(coins[i].askSat * .0025) && coins[i].spread < ceil(coins[i].askSat * bot.margin))
					{
						// no way to predict where the trend will go except for looking at the order book and size of impacting orders
						// for now, clamp it equally and hope
						if (!buyCoins(&coins[i], (int)floor(((coins[i].askSat - (coins[i].spread / 2)) * (1 - (bot.margin / 2))))))
						{
							break;
						}
						if (!sellCoins(&coins[i], (int)floor(((coins[i].askSat - (coins[i].spread / 2)) * (1 + (bot.margin / 2))))))
						{
							break;
						}
						coins[i].order = ORDER_STATE_ACTIVE;
						time(&coins[i].orderTimer);
						break;
					}
				}
				*/
				case TREND_STATE_INCREASING:
				{
					// must prepare for possibility of client side transaction failure on only 1, in which case, the other must be immediately canceled
					// consider d/dt of previous hour and day
					
					if (coins[i].spread > 1 && coins[i].spread < coins[i].askSat * bot.margin)
					{
						if (!buyCoins(&coins[i], (int)floor(coins[i].bidSat * 1.001)))
						{
							break;
						}
						if (!sellCoins(&coins[i], (int)floor(coins[i].bidSat * 1.001 * (1 + bot.margin))))
						{
							break;
						}
						coins[i].order = ORDER_STATE_ACTIVE;
						time(&coins[i].orderTimer);
						time(&coins[i].probeTimer);
					}
					break;
				}
				case TREND_STATE_DECREASING:
				{
					// must prepare for possibility of transaction failure on only 1, in which case, the other must be immediately canceled
					// consider d/dt of previous hour and day
					
					if (coins[i].spread > 1 && coins[i].spread < coins[i].askSat * bot.margin)
					{
						if (!sellCoins(&coins[i], (int)floor(coins[i].askSat * .999)))
						{
							break;
						}
						if (!buyCoins(&coins[i], (int)floor(coins[i].askSat * .999 * (1 - bot.margin))))
						{
							break;
						}
						coins[i].order = ORDER_STATE_ACTIVE;
						time(&coins[i].orderTimer);
						time(&coins[i].probeTimer);
					}
					break;
				}
				}
				break;
			}
			case ORDER_STATE_ACTIVE:
			{
				// probe order status every 10 seconds
				if (difftime(time(NULL), coins[i].probeTimer) > 10)
				{
					printf("Probing %s's order status...\n", coins[i].name);
					time(&coins[i].probeTimer);

					// order assessment
					if (getOrder(&coins[i].buyUUID[0]) == 1)
					{
						if (getOrder(&coins[i].sellUUID[0]) == 1)
						{
							coins[i].order = ORDER_STATE_EMPTY;
							bot.transactions++;
							bot.tPerHour = (bot.transactions * 360) / difftime(time(NULL), bot.timer);
							printf("A transaction for %s has completed.\nI have made %i transactions at a rate of %f per hour.\nSince coming online, I have multiplied your current assets by %f.\n", coins[i].name, bot.transactions, bot.tPerHour, pow(1.0025, (double)bot.transactions));
							/*
							if (bot.tPerMin < 1)
							{
								printf("My optimal rate is at least 1 transaction per minute. Consider adding more coins and replacing the stagnant...\n");
							}

							if (bot.tPerMin > 2)
							{
								printf("The %s market exhibits great volatility. I have increased it's trade margin to 1%...\n");
								bot.margin = .01;
							}
							*/
							getCoinBalance(&coins[i]);
							break;
						}
					}
					// if order exceeds 30 minutes, cancel and consider it a failure
					if (difftime(time(NULL), coins[i].orderTimer) > 3600)
					{
						if (getOrder(&coins[i].buyUUID[0]) == 0)
						{
							if (getOrder(&coins[i].sellUUID[0]) == 0)
							{
								if (!cancelOrder(&coins[i].sellUUID[0]))
									break;
								if (!cancelOrder(&coins[i].buyUUID[0]))
									break;
							}
							if (!cancelOrder(&coins[i].buyUUID[0]))
								break;
						}
						if (getOrder(&coins[i].sellUUID[0]) == 0)
						{
							if(!cancelOrder(&coins[i].sellUUID[0]))
								break;
						}
					}

						/*
						// cancel sell after 10 minutes
						if (difftime(time(NULL), coins[i].orderTimer) > 600)
						{
							if (!cancelOrder(&coins[i].sellUUID[0]))
								break;
							bot.mistakes++;
							printf("I have made a faulty transaction. Total: %i\n", bot.mistakes);
							coins[i].order = ORDER_STATE_EMPTY;
							coins[i].trend = TREND_STATE_UNDEFINED;
							getCoinBalance(&coins[i]);
							break;
						}
						*/
					
					/*
					if (getOrder(&coins[i].sellUUID[0]) == 1)
					{
						// cancel buy after 10 minutes
						if (difftime(time(NULL), coins[i].orderTimer) > 600)
						{
							if (!cancelOrder(&coins[i].buyUUID[0]))
								break;
							bot.mistakes++;
							printf("I have made a faulty transaction. Total: %i\n", bot.mistakes);
							coins[i].order = ORDER_STATE_EMPTY;
							coins[i].trend = TREND_STATE_UNDEFINED;
							getCoinBalance(&coins[i]);
							break;
						}
					}
					// cancel both after 10 minutes
					if (difftime(time(NULL), coins[i].orderTimer) > 600)
					{
						if (!cancelOrder(&coins[i].sellUUID[0]))
							break;
						if (!cancelOrder(&coins[i].buyUUID[0]))
							break;
						bot.mistakes++;
						printf("I have made a faulty transaction. Total: %i\n", bot.mistakes);
						coins[i].order = ORDER_STATE_EMPTY;
						coins[i].trend = TREND_STATE_UNDEFINED;
						getCoinBalance(&coins[i]);
						break;
					}
					*/
				}
			}
			}

		}
		Sleep(500); // .5 second queries
	}
	
	disconnectBittrex();

	//free(coins);
	//coins = NULL;

	getchar();
	return 0;
}