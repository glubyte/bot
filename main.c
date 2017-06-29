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
	RIBBON_STATE_MOON,
	RIBBON_STATE_CRASH,
	RIBBON_STATE_KNOT,
	RIBBON_STATE_COIL,
	RIBBON_STATE_TWIST,
	RIBBON_STATE_TWIRL,
	RIBBON_STATE_BUNDLE,
	RIBBON_STATE_FLAT
} ribbonState;

typedef enum {
	LACE_STATE_TIGHT,
	LACE_STATE_LOOSE,
	LACE_STATE_DIVERGENCE,
	LACE_STATE_UNTIED
} laceState;

typedef enum {
	ORDER_STATE_BUY,
	ORDER_STATE_SELL,
	ORDER_STATE_EMPTY
} orderState;

typedef struct {
	ribbonState ribbon;
	laceState lace;
	orderState order;
	time_t orderTimer;
	time_t probeTimer;
	float data[360];
	unsigned int minuteAverage;
	unsigned int tenMinuteAverage;
	unsigned int hrMovingAverage;
	unsigned int minuteHigh;
	unsigned int tenMinuteHigh;
	unsigned int hrHigh;
	unsigned int minuteLow;
	unsigned int tenMinuteLow;
	unsigned int hrLow;
	int lace1;
	int lace2;
	int lace3;
	int lace1High;
	int lace2High;
	int lace3High;
	int lace1Low;
	int lace2Low;
	int lace3Low;
	int sellSignals;
	int buySignals;
	float beta0;
	float beta1;
	char name[5];
	float num;
	float askSat;
	float bidSat;
	float lastSat;
	float spread;
	char btcValue[11];
	float btcNet;
	int deviation;
	char sellUUID[37];
	char buyUUID[37];
} coin;

typedef struct {
	int transactions;
	int mistakes;
	float tPerHour;
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
int poolSize = 360; // must be even
float filter = .001; // fluctuation filter
int period = 10000; // 10 second period

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
float leastSquares(float* coinData);
void movingAverage(float* data, int points, int* movingAverage);
void fillData(coin* coins, int points, int numCoins);
void updateData(coin* coin);
void tieRibbon(coin* coin);
void checkLaces(coin* coin);

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
	// HERE BE MACDNESS

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

		coins[i].lace1 = 0;
		coins[i].lace2 = 0;
		coins[i].lace3 = 0;
		coins[i].sellSignals = 0;
		coins[i].buySignals = 0;
		coins[i].lace3High = 0;
		coins[i].lace3Low = 0;

		// initialize buffers
		for (int j = 0; j < poolSize; j++)
		{
			coins[i].data[j] = 0;
		}

		coins[i].order = ORDER_STATE_SELL;
		coins[i].ribbon = RIBBON_STATE_FLAT;
		coins[i].lace = LACE_STATE_UNTIED;

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
	coin->buyUUID[36] = '\0';

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
	int received;

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
	coin->sellUUID[36] = '\0';

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
	char lastBuff[32];
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
		strncpy(lastBuff, strstr(&reply[0], "Last\"") + 9, 7);
	}
	askBuff[10] = '\0';
	bidBuff[10] = '\0';
	lastBuff[10] = '\0';

	// assign new market values
	coin->askSat = atoi(askBuff);
	coin->bidSat = atoi(bidBuff);
	coin->lastSat = atoi(lastBuff);
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

float leastSquares(float* coinData)
{
	int i = 0;
	int points = sizeof(coinData) / sizeof(float);
	float s1 = 0, s2 = 0, s3 = 0, s4 = 0;

	for (i = 0; i < points; i++)
	{
		s4 += coinData[i];
		s3 += 10 * i;
		s2 += (10 * i) * (10 * i);
		s1 += coinData[i] * (10 * i);
	}

	return (points * s1 - s3 * s4) / (points * s2 - s3 * s3);
}

void movingAverage(float* data, int points, int* movingAverage)
{
	unsigned int s = 0;

	for (int i = 0; i < points; i++)
	{
		s += data[i];
	}

	*movingAverage = (int)((float)s / points);
}

void fillData(coin* coins, int points, int numCoins)
{
	int dataCounter = poolSize - 1;
	int i = 0;

	// fill pools
	while (dataCounter >= 0)
	{
		for (i = 0; i < numCoins; i++)
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

			// dump in pool
			coins[i].data[dataCounter] = coins[i].lastSat;
			printf("%s data point %i is %i.\n", coins[i].name, dataCounter + 1, (int)coins[i].lastSat);
		}
		dataCounter--;
		Sleep(period);
	}

	for (i = 0; i < numCoins; i++)
	{
		// 1 minute average
		movingAverage(coins[i].data, 6, &coins[i].minuteAverage);
		// 10 minute average
		movingAverage(coins[i].data, 60, &coins[i].tenMinuteAverage);
		// 1 hour average
		movingAverage(coins[i].data, poolSize, &coins[i].hrMovingAverage);
	}
}

void updateData(coin* coin)
{
	if (!getCoinPrice(coin))
	{
		if (!getCoinPrice(coin))
		{
			printf("Fatal network error.\n");
		}
	}

	// new_average = old_average - (point_oldest / n) + (point_newest / n)
	// shift old data pool, insert new value
	coin->minuteAverage = coin->minuteAverage - (coin->data[5] / 6);
	coin->tenMinuteAverage = coin->tenMinuteAverage - (coin->data[59] / 60);
	coin->hrMovingAverage = coin->hrMovingAverage - (coin->data[poolSize - 1] / poolSize);

	memmove(&coin->data[1], &coin->data[0], sizeof(float) * (poolSize - 1));
	coin->data[0] = coin->lastSat;

	coin->minuteAverage = coin->minuteAverage + (coin->data[0] / 6);
	coin->tenMinuteAverage = coin->tenMinuteAverage + (coin->data[0] / 60);
	coin->hrMovingAverage = coin->hrMovingAverage + (coin->data[0] / poolSize);

	coin->lace1 = coin->minuteAverage - coin->tenMinuteAverage;
	coin->lace2 = coin->minuteAverage - coin->hrMovingAverage;
	coin->lace3 = coin->tenMinuteAverage - coin->hrMovingAverage;
	printf("%s Laces: %i, %i, %i.\n", coin->name, coin->lace1, coin->lace2, coin->lace3);
	printf("%s has a 1 minute MA of %i sat.\n", coin->name, (int)coin->minuteAverage);
	printf("%s has a 10 minute MA of %i sat.\n", coin->name, (int)coin->tenMinuteAverage);
	printf("%s has a 1 hour MA of %i sat.\n", coin->name, (int)coin->hrMovingAverage);
}

void tieRibbon(coin* coin)
{
	// apply fluctuation filter
	if (abs(coin->lace1) < coin->lastSat * filter)
	{
		printf("%s Lace 1 overlap detected. This could imply multiple things.\n", coin->name);
		if (abs(coin->lace3) < coin->lastSat * filter)
		{
			printf("%s is bundling...\n");
			coin->ribbon = RIBBON_STATE_BUNDLE;
		}
	}
	else if (coin->lace1 > 0)
	{
		if (coin->lace3 > 0)
		{
			if ((coin->ribbon == RIBBON_STATE_KNOT || coin->ribbon == RIBBON_STATE_COIL) && coin->sellSignals > 0)
			{
				printf("%s was disentangled. Removing sell signal...\n", coin->name);
				coin->sellSignals--;
			}
			coin->ribbon = RIBBON_STATE_MOON;
			printf("%s is mooning...\n", coin->name);
		}
		else if (coin->lace2 > 0)
		{
			if (coin->ribbon == RIBBON_STATE_CRASH)
			{
				printf("%s is becoming entangled. This is a buy signal.\n", coin->name);
				coin->buySignals++;
			}
			coin->ribbon = RIBBON_STATE_TWIST;
			printf("%s is twisting...\n", coin->name);
		}
		else
		{
			if (coin->ribbon == RIBBON_STATE_CRASH)
			{
				printf("%s is becoming entangled. This is a buy signal.\n", coin->name);
				coin->buySignals++;
			}
			coin->ribbon = RIBBON_STATE_TWIRL;
			printf("%s is twirling...\n", coin->name);
		}
	}
	else if (coin->lace3 > 0)
	{
		if (coin->lace2 > 0)
		{
			if (coin->ribbon == RIBBON_STATE_MOON)
			{
				printf("%s is becoming entangled. This is a sell signal.\n", coin->name);
				coin->sellSignals++;
			}
			coin->ribbon = RIBBON_STATE_KNOT;
			printf("%s is knotting...\n", coin->name);
		}
		else
		{
			if (coin->ribbon == RIBBON_STATE_MOON)
			{
				printf("%s is becoming entangled. This is a sell signal.\n", coin->name);
				coin->sellSignals++;
			}
			coin->ribbon = RIBBON_STATE_COIL;
			printf("%s is coiling...\n", coin->name);
		}
	}
	else
	{
		if ((coin->ribbon == RIBBON_STATE_TWIST || coin->ribbon == RIBBON_STATE_TWIRL) && coin->buySignals > 0)
		{
			printf("%s was disentangled. Removing buy signal...\n", coin->name);
			coin->buySignals--;
		}
		coin->ribbon = RIBBON_STATE_CRASH;
		printf("%s is crashing...\n", coin->name);
	}
}

void checkLaces(coin* coin)
{
	switch (coin->order)
	{
	case ORDER_STATE_SELL:
	{
		if (coin->lace3 > coin->lace3High)
		{
			if (coin->lace == LACE_STATE_DIVERGENCE)
			{
				printf("%s has reformed from the divergence. Removing sell signal...\n", coin->name);
				coin->sellSignals--;
			}
			else if (coin->lace == LACE_STATE_TIGHT)
			{
				printf("%s loosened up. Removing sell signal...\n", coin->name);
				coin->sellSignals--;
			}
			coin->lace3High = coin->lace3;
			coin->tenMinuteHigh = coin->tenMinuteAverage;
			coin->lace = LACE_STATE_LOOSE;
		}
		else if (coin->tenMinuteAverage > coin->tenMinuteHigh)
		{
			if (coin->lace != LACE_STATE_DIVERGENCE)
			{
				printf("%s bearish divergence detected. Possible impending recoil...\n", coin->name);
				coin->sellSignals++;
				coin->lace = LACE_STATE_DIVERGENCE;
			}
			coin->tenMinuteHigh = coin->tenMinuteAverage;
		}
		else
		{
			printf("%s tightening up...\n", coin->name);
			if (coin->lace == LACE_STATE_DIVERGENCE)
			{
				printf("This has followed a divergence. This is a strong sell signal.\n");
			}
			else if (coin->lace == LACE_STATE_LOOSE)
			{
				printf("No divergence detected. This is a weak sell signal.\n");
			}
			coin->lace = LACE_STATE_TIGHT;
		}
		break;
	}
	case ORDER_STATE_BUY:
	{
		if (coin->lace3 < coin->lace3Low)
		{
			if (coin->lace == LACE_STATE_DIVERGENCE)
			{
				printf("%s has reformed from the divergence. Removing buy signal...\n", coin->name);
				coin->buySignals--;
			}
			else if (coin->lace == LACE_STATE_TIGHT)
			{
				printf("%s loosened up. Removing buy signal...\n", coin->name);
				coin->buySignals--;
			}
			coin->lace3Low = coin->lace3;
			coin->tenMinuteLow = coin->tenMinuteAverage;
			coin->lace = LACE_STATE_LOOSE;
		}
		else if (coin->tenMinuteAverage < coin->tenMinuteLow)
		{
			if (coin->lace != LACE_STATE_DIVERGENCE)
			{
				printf("%s bullish divergence detected. Possible impending recoil...\n", coin->name);
				coin->buySignals++;
				coin->lace = LACE_STATE_DIVERGENCE;
			}
			coin->tenMinuteLow = coin->tenMinuteAverage;
		}
		else
		{
			printf("%s tightening up...\n", coin->name);
			if (coin->lace == LACE_STATE_DIVERGENCE)
			{
				printf("This has followed a divergence. This is a strong buy signal.\n");
			}
			else if (coin->lace == LACE_STATE_LOOSE)
			{
				printf("No divergence detected. This is a weak buy signal.\n");
			}
			coin->lace = LACE_STATE_TIGHT;
		}
		break;
	}
	}
}

int main(void)
{
	if (!connectBittrex())
	{
		getchar();
		return 0;
	}

	// update whenever coins are added or removed
	unsigned int numCoins = 1;

	printf("Giving life to bot...\n");
	botData bot;
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
	coin* coins = (coin*)malloc(sizeof(coin) * numCoins);
	initCoins(coins, numCoins);
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
	printf("Waiting 1 hour to fill data pool...\n");
	fillData(coins, poolSize, numCoins);
	Sleep(2000);
	printf("Observing...\n");
	while (i)
	{
		for (i = 0; i < numCoins; i++)
		{
			updateData(&coins[i]);
			tieRibbon(&coins[i]);
			checkLaces(&coins[i]);
			
			printf("%s has %i buy and %i sell signals.\n", coins[i].name, coins[i].buySignals, coins[i].sellSignals);
			
			// old shit
			/*
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
				case TREND_STATE_STAGNANT:
				case TREND_STATE_OSCILLATORY:
				{
					
					if (coins[i].spread > ceil(coins[i].askSat * .0025))
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
						printf("Probing %s's order status...\n", coins[i].name);
						coins[i].order = ORDER_STATE_ACTIVE;
						time(&coins[i].orderTimer);
						time(&coins[i].probeTimer);
						break;
					}
				}
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
						printf("Probing %s's order status...\n", coins[i].name);
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
						printf("Probing %s's order status...\n", coins[i].name);
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
					time(&coins[i].probeTimer);

					// order assessment
					if (getOrder(&coins[i].buyUUID[0]) == 1)
					{
						if (getOrder(&coins[i].sellUUID[0]) == 1)
						{
							coins[i].order = ORDER_STATE_EMPTY;
							bot.transactions++;
							bot.tPerHour = (bot.transactions * 3600) / difftime(time(NULL), bot.timer);
							printf("A transaction for %s has completed.\nI have MACDe %i transactions at a rate of %f per hour.\nSince coming online, I have multiplied your current assets by %f.\n", coins[i].name, bot.transactions, bot.tPerHour, pow(1.0025, (double)bot.transactions));
							getCoinBalance(&coins[i]);
							break;
						}
					}
				}
			}
			}
			*/
		}
		Sleep(period);
	}
	
	disconnectBittrex();

	//free(coins);
	//coins = NULL;

	getchar();
	return 0;
}