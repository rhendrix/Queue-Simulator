#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <vector>
#include <numeric>
#include <algorithm>
#include <atomic> 
#include <iostream>

/*
All times in microseconds

NUM_CASHIERS       Number of cashiers
TIME_OPEN          How long to stay open
TIME_SCALE         How much to scale down time
CUSTOMER_CHANCE    The chance of a customer coming in the given time period
CUSTOMER_FREQUENCY The time period mentioned above
ITEM_TIME          The time it takes to scan and bag an item
MONEY_TIME         The time it takes to take a customers money
DRAW_TIME          How often to draw registers
*/

#define NUM_CASHIERS       4
#define TIME_OPEN          50400000000
#define TIME_SCALE         5000
#define CUSTOMER_CHANCE    63 
#define CUSTOMER_FREQUENCY 30000000
#define ITEM_TIME          5000000
#define MONEY_TIME         30000000
#define DRAW_TIME          30000000

using namespace std;

class customer
{
	public:
		/*
		items   number of items customer has
		entered the time the customer enters the queue
		next    the next customer in line (NULL last customer)
		*/
		int items;
		unsigned long entered;
		customer* next;
		
		//creates customer with random number of items between 
		//1 and 60
		customer()
		{
			items = rand() % 60 +1;
			timeval tv;
			gettimeofday(&tv, NULL);
			entered = 1000000*tv.tv_sec + tv.tv_usec;
			next = NULL;
		}
};

class queue
{
	public:
		/*
		maxItems     customers with more than maxItems will not enter
		customers    the current number of customers
		maxCustomers the longest the queue has ever been 
		first        the first customer in line
		*/
		int maxItems, maxCustomers;
		atomic_int customers;
		customer* first;
		
		//creates a queue with no customers and no item limit
		queue()
		{
			first = NULL;
			maxItems = 1000;
		}

		//creates a queue with given item limit
		queue(int m)
		{
			first = NULL;
			maxItems = m;
		}

		//removes first customer from queue
		//returns customers wait time
		int checkOut()
		{
			//calculate wait time
			timeval tv;
			gettimeofday(&tv, NULL);
			unsigned long retval;
			retval = (1000000*tv.tv_sec + tv.tv_usec - first->entered) * TIME_SCALE;

			//Make next custoner first and free old first
			customer* tmp = first;
			first = first->next;
			free(tmp);
			customers--;

			return retval;
		}

		void addCustomer(customer* c)
		{
			customers++;

			//check if queue is longer than maxCustomers
			if(customers > maxCustomers)
			{
				maxCustomers = customers;
			}

			//check if queue empty
			if(first == NULL)
			{
				first = c;
				return;
			}

			//find last in line
			customer* cur = first;
			while(cur->next != NULL)
				cur = cur->next;

			//add customer to end of line
			cur->next = c;
		}
};

/*
cashiers      array of cashier threads
queues        array of queues
numCustomers  number of customers that have entered store
registersOpen flag for registers still being open 
storeOpen     flag for store still being open
waitTimes     vector of all customer wait times
*/

pthread_t cashiers[NUM_CASHIERS];
queue queues[NUM_CASHIERS];
atomic_int numCustomers, registersOpen, storeOpen;
vector<unsigned long> waitTimes;

//adds customer to shortedt viable queue
void queueUp(customer* c)
{
	//find shortest queue with maxItems less than customers items
	int i = 0;
	while(c->items > queues[i++].maxItems){}
	int argmin = i-1;
	int min = queues[argmin].customers;
	for(;i<NUM_CASHIERS;i++)
	{
		if(queues[i].customers <= min && c->items <= queues[i].maxItems)
		{
			if(queues[i].customers == min)
			{
				if(queues[argmin].maxItems < queues[i].maxItems)
					continue;
			}
			min = queues[i].customers;
			argmin = i;
		}
	}

	//add customer to shortest queue
	queues[argmin].addCustomer(c);
}

//function for cashier threads
void* openReg(void* line)
{
	unsigned long checkoutTime;
	queue* q = (queue*)line;

	//open register
	registersOpen++;

	//checkout customers until store closes and all customers out
	while(storeOpen || q->customers)
	{
		//check for customers
		if(q->first != NULL)
		{
			//calculate checkout time
			checkoutTime = (ITEM_TIME * q->first->items + MONEY_TIME)/TIME_SCALE;

			//checkout customer
			waitTimes.push_back(q->checkOut());

			usleep(checkoutTime);
		}
	}
	
	//close register
	registersOpen--;

	pthread_exit(NULL);
}

//report statistics for the day
void report()
{
	//calculate average of wait times
	double sum = accumulate(waitTimes.begin(), waitTimes.end(), 0.0);
	double mean = sum / waitTimes.size();

	//find maximum wait time
	double max = *max_element(waitTimes.begin(), waitTimes.end());

	//find longest queue length
	int maxQueueLength = 0;
	for(int i=0;i<NUM_CASHIERS;i++)
	{
		if(queues[i].maxCustomers > maxQueueLength)
		{
			maxQueueLength = queues[i].maxCustomers;
		}
	}

	//print report
	cout << "Customers: " << numCustomers << endl;
	cout << "Average Wait Time: " << mean/1000000 << " seconds" << endl;
	cout << "Maximum Wait Time: " << max/1000000 << " seconds" << endl;
	cout << "Longest Queue Length: " << maxQueueLength << endl;
}

//function for draw thread
void* draw(void* n)
{
	while(registersOpen || storeOpen)
	{
		//clear screen
		cout << string(200, '\n');

		//print status of store and registers
		cout << "Store Open: " << storeOpen << endl;
		cout << "Registers Open: " << registersOpen << endl;

		//draw registers
		for(int i=0;i<NUM_CASHIERS;i++)
		{
			cout << 'X' << endl;
			cout << string(queues[i].customers, '0') << "\n\n";
		}

		//sleep
		usleep(DRAW_TIME/TIME_SCALE);
	}

	pthread_exit(NULL);
}

int main(int argc, char** argv)
{
	//check for -d and set draw flag
	bool drawFlag = 0;
	if(argc == 2)
	{
		if(!strcmp("-d", argv[1]))
		{
			drawFlag = 1;
		}
	}

	int rc, chance;
	customer c;
	registersOpen = 0;

	//calculate open and close times
	timeval tv;
	gettimeofday(&tv, NULL);
	unsigned long openTime, closeTime, currentTime;
	openTime = 1000000*tv.tv_sec + tv.tv_usec;
	closeTime = openTime + TIME_OPEN/TIME_SCALE;

	//open store
	storeOpen = 1;

	//create cashier threads
	for(int i=0;i<NUM_CASHIERS;i++)
	{
		rc = pthread_create(&cashiers[i], NULL, openReg, (void *) &queues[i]);
		pthread_detach(cashiers[i]);
	}

	//if draw flag is set create draw flag
	if(drawFlag)
	{
		pthread_t drawThread;
		rc = pthread_create(&drawThread, NULL, draw, NULL);
	}

	//get current time
	gettimeofday(&tv, NULL);
	currentTime = 1000000*tv.tv_sec + tv.tv_usec;

	//run store while currentTime is before closeTime
	while(currentTime < closeTime)
	{
		//get random number between 0 and 100
		chance = rand() % 100 + 1;

		//check if customer needs to be created
		if(chance <= CUSTOMER_CHANCE)
		{
			//create new customer
			numCustomers++;
			queueUp(new customer());
		}

		//sleep for customer frequency
		usleep(CUSTOMER_FREQUENCY/TIME_SCALE);

		//get current time
		gettimeofday(&tv, NULL);
		currentTime = 1000000*tv.tv_sec + tv.tv_usec;
	}

	//close store
	storeOpen = 0;
	
	//wait for registers to close
	while(registersOpen){}

	//make report
	report();

	pthread_exit(NULL);
	return 0;
}
