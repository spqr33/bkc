#include <iostream>
#include <queue>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <cstdint>
#include <memory>
#include <windows.h>
#include <fstream>
#include <ctime>
#include <cstdlib>

using namespace std;

typedef uint8_t BYTE;
typedef struct tagTDATA {
	BYTE cPriority; //приоритет запроса 0 – 255 (0 – наивысший приоритет)
	DWORD dwTicks; //время формирования запроса в системных тиках // DWORD - мало
	DWORD dwClientId; //уникальный идентификатор клиента
	char Data[255]; //абстрактные данные
} TDATA, *PTDATA;
typedef shared_ptr<TDATA> SPTDATA;

bool SPTDATA_comparator(SPTDATA a, SPTDATA b) {
	if (a->cPriority > b->cPriority) {
		return true;
	}
	return false;
}

struct MessagesExchangePoint {
	typedef priority_queue<SPTDATA, vector<SPTDATA>, function<bool(SPTDATA, SPTDATA)> > MessagePriorityQueue;
	MessagePriorityQueue message_queue_;
	mutex mutex_message_queue_;

	bool must_exit;
	mutex mutex_must_exit;


	static MessagesExchangePoint& instance() {
		static MessagesExchangePoint single;

		return single;
	}
private:
	MessagesExchangePoint() :
		message_queue_(SPTDATA_comparator), must_exit(false) {}
	MessagesExchangePoint(const MessagesExchangePoint&);
	MessagesExchangePoint& operator=(const MessagesExchangePoint&);
};

class Client {
public:
	Client(uint8_t priority, uint8_t id) :
		priority_(priority),
		id_(id) {};

	void operator()(MessagesExchangePoint& message_exchange_point) {
		srand((static_cast<uint32_t>(time(NULL)) << id_) + priority_);
		const unsigned MAX_MILLISECONDS_SLEEP = 200;

		while (true) {
			{
				lock_guard<mutex> lock(message_exchange_point.mutex_must_exit);
				if (message_exchange_point.must_exit == true) {
					break;
				}
			}
			this_thread::sleep_for(std::chrono::milliseconds(rand() % MAX_MILLISECONDS_SLEEP));
			SPTDATA sp_message = generate_message();
			{
				lock_guard<mutex> lock(message_exchange_point.mutex_message_queue_);
				message_exchange_point.message_queue_.push(sp_message); 
			}
		}
	};
private:
	uint8_t		priority_;
	uint8_t		id_;

	SPTDATA generate_message() {
		PTDATA p_message = new TDATA;
		p_message->cPriority = priority_;
		p_message->dwClientId = id_;
		p_message->dwTicks = GetTickCount();
		for (uint32_t i = 0; i < sizeof(p_message->Data); ++i)
		{
			p_message->Data[i] = id_; // place smth 
		}
		SPTDATA sp_message(p_message);

		return sp_message;
	};
};

class Server {
public:
	enum LogFailureBehavior { PROGRAM_EXIT, SEND_ALARM };
	Server(uint32_t clients_quantity, ofstream& output_file, LogFailureBehavior behavior = PROGRAM_EXIT) :
		clients_quantity_(clients_quantity),
		output_file_(output_file),
		behavior_(behavior)	{};
	void operator()(MessagesExchangePoint& message_exchange_point) {
		while ( true ) {
			{
				lock_guard<mutex> lock(message_exchange_point.mutex_must_exit);
				if (message_exchange_point.must_exit == true) {
					break;
				}
			}
			
			bool has_message_to_process = false;
			SPTDATA sp_message;
			{
				lock_guard<mutex> lock(message_exchange_point.mutex_message_queue_);
				if (!message_exchange_point.message_queue_.empty()) {
					sp_message = message_exchange_point.message_queue_.top();
					message_exchange_point.message_queue_.pop();
					has_message_to_process = true;
				}
			}
			if (!has_message_to_process) {
				this_thread::yield();
				continue;
			} else { 
				log_data(sp_message);
			}
		}
	};

private:
	uint32_t			clients_quantity_;
	ofstream&			output_file_;
	LogFailureBehavior	behavior_;

	void log_data(SPTDATA& sp_message) {
		output_file_ << GetTickCount() << '\t'
			<< sp_message->dwClientId << '\t'
			<< static_cast<uint32_t>(sp_message->cPriority) << '\t'
			<< sp_message->dwTicks << endl;

		if (!output_file_.good()) {
			switch (behavior_){
			case Server::PROGRAM_EXIT:
				exit(EXIT_FAILURE);
				break;
			case Server::SEND_ALARM:
				send_alarm();
				break;
			}
		}
	};
	void send_alarm() {
		//do smth;
	};
};

uint8_t get_clients_quantity(uint32_t maximum) {
	uint32_t quantity;

	while (true) {
		cout << "Enter up to " << maximum << " clients, please" << endl;
		cin >> quantity;
		if (!cin) {
			cin.clear();
			while (cin.get() != '\n') continue;
		}
		else if (quantity <= maximum) {
			return quantity;
		}
	}
};

int main() {
	const uint8_t			PRIORITY_LEVELS = 255;
	const uint32_t			MAXIMUM_CLIENT_QUANTITY = 10;
	uint32_t				clients_quantity = get_clients_quantity(MAXIMUM_CLIENT_QUANTITY);
	string					output_file_name = "messages.log";
	ofstream				output_file(output_file_name, ios_base::out | ios_base::trunc);
	if (!output_file.is_open()) {
		cout << "Couldn't open  " << output_file_name.c_str() << endl;
		output_file.clear();

		return EXIT_FAILURE;
	}
	srand(static_cast<uint32_t>(time(NULL)));

	MessagesExchangePoint& exchange_point = MessagesExchangePoint::instance();
	vector<thread> clients_threads(clients_quantity);
	for (uint32_t i = 1; i <= clients_quantity; ++i) {
		uint8_t priority = rand() % (PRIORITY_LEVELS + 1);

		clients_threads.emplace_back(Client(priority, i), std::ref(exchange_point));
		//clients_threads.back().detach();
	}
	thread server_thread(Server(clients_quantity, output_file), std::ref(exchange_point));

	cout << "Press Enter to finish\n";
	while (cin.get() != '\n') continue;
	char any_key;
	any_key = cin.get();
	{
		lock_guard<mutex> lock(exchange_point.mutex_must_exit);
		exchange_point.must_exit = true;
	}

	server_thread.join();
	for (uint32_t i = 0; i < clients_threads.size(); ++i) {
		thread t(std::move(clients_threads.back()));
		clients_threads.pop_back();
		t.join();
		//clients_threads[i].join(); // didn't compile w this
	}
	
	output_file.clear();
	output_file.close();
	return EXIT_SUCCESS;
}