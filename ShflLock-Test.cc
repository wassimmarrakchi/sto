#include <ctime>
#include <cstdint>
#include <atomic>
#include <cassert>
#include <iostream>
#include <stdio.h>
#include "ShflLock.hh"
#include <experimental/random>
#include <vector>
#include <thread>
#include <unistd.h>

struct account {
	struct lock password;
	int balance = 100;	
};

struct account bank[100];

void BankTest() {
	std::thread::id thread_id = std::this_thread::get_id();
	printf("Thread %lli has started.\n", (long long int) &thread_id);
	for(int i = 0; i < 10000; i++) {
		int account_nbr_send = std::experimental::randint(0, 99);
		spin_lock(&bank[account_nbr_send].password);
		int amount_sub = std::experimental::randint(1,bank[account_nbr_send].balance);
		int account_nbr_receive = std::experimental::randint(0, 99);
		spin_lock(&bank[account_nbr_receive].password);
		bank[account_nbr_send].balance -= amount_sub;
		bank[account_nbr_receive].balance += amount_sub;
		spin_unlock(&bank[account_nbr_receive].password);
		spin_unlock(&bank[account_nbr_send].password);
	};
	printf("Thread %lli has completed.\n", (long long int) &thread_id);
};

int main() {
	int nbr_threads = 1;

	std::thread threads[nbr_threads];

	printf("Starting off four threads of bank transactions...\n");
	for(int i = 0; i < nbr_threads; i++) {
		threads[i] = std::thread(BankTest);
	};

	printf("Waiting for threads to finish...\n");
	// Wait for the threads to finish,
	for (int i = 0; i < nbr_threads; i++) {
		threads[i].join();
	};

	usleep(10000000);

	printf("Threads completed. Calculating total balance across accounts...\n");
	int total = 0;
	for (int i = 0; i < 100; i++) {
		total += bank[i].balance;
	}

	printf("The total balance across the accounts is: %i", total);

	return 0;
};