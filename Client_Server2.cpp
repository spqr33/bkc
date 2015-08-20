#include <cstdint>
#include <windows.h>
#include <memory>
#include <queue>
#include <vector>
#include <functional>
#include <process.h>
#include <ctime>
#include <fstream>
#include <iostream>

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

class Mutex {
public:
	Mutex(){ InitializeCriticalSection(&cs_); };
	void lock() { EnterCriticalSection(&cs_); };
	void unlock() { LeaveCriticalSection(&cs_); };
	~Mutex() {  DeleteCriticalSection(&cs_); };
private:
	CRITICAL_SECTION cs_;

	Mutex(const Mutex& orig);
	Mutex operator=(const Mutex& rhs);
};

template<typename T>
class MutexGuard {
public:
	explicit MutexGuard(T& locable_entity) : locable_(locable_entity) { locable_.lock(); };
	~MutexGuard() { locable_.unlock(); }
private:
	T& locable_;

	MutexGuard(const MutexGuard& orig);
	MutexGuard& operator=(const MutexGuard& rhs);
};

struct MessagesExchangePoint {
	typedef priority_queue<SPTDATA, vector<SPTDATA>, function<bool(SPTDATA, SPTDATA)> > MessagePriorityQueue;
	MessagePriorityQueue message_queue_;
	Mutex mutex_message_queue_;

	static MessagesExchangePoint& instance() {
		static MessagesExchangePoint single;

		return single;
	}
private:
	MessagesExchangePoint() :
		message_queue_(SPTDATA_comparator) {}
	MessagesExchangePoint(const MessagesExchangePoint&);
	MessagesExchangePoint& operator=(const MessagesExchangePoint&);
};

class Client {
public:
	Client(MessagesExchangePoint& ex_point, uint8_t priority, uint8_t id) 
		: exchange_point_(ex_point), priority_(priority), id_(id) {};
	~Client() {};
	void do_work() {
		handle_ = (HANDLE)_beginthreadex(NULL, 0, worker, this, 0, 0);
	};
	void join() {
		while ( WaitForSingleObject(handle_, INFINITE) != WAIT_OBJECT_0 );
		CloseHandle(handle_);
	};
private:
	HANDLE  handle_;
	MessagesExchangePoint& exchange_point_;
	uint8_t		priority_;
	uint8_t		id_;

	Client(const Client& orig);
	Client& operator=(const Client& rhs);

	static unsigned int WINAPI worker(void* p_to_this) {
		Client *c = static_cast<Client*>(p_to_this);
				
		SPTDATA sp_message = c->generate_message();
		MutexGuard<Mutex> lock(c->exchange_point_.mutex_message_queue_);
		c->exchange_point_.message_queue_.push(sp_message);
		
		return 0;
	}
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
	Server(MessagesExchangePoint& ex_point, uint32_t clients_quantity, ofstream& output_file, LogFailureBehavior behavior = PROGRAM_EXIT) :
		exchange_point_(ex_point),
		clients_quantity_(clients_quantity),
		output_file_(output_file),
		behavior_(behavior)
		{};
	void do_work() {
		handle_ = (HANDLE)_beginthreadex(NULL, 0, worker, this, 0, 0);
	};
	void join() {
		while ( WaitForSingleObject(handle_, INFINITE) != WAIT_OBJECT_0 );
		CloseHandle(handle_);
	};
private:
	HANDLE  handle_;
	MessagesExchangePoint& exchange_point_;
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
	static unsigned int WINAPI worker(void* p_to_this) {
		Server *s = static_cast<Server*>(p_to_this);
				
		uint32_t messages_recieved = 0;

		while (messages_recieved < s->clients_quantity_) {
			bool has_message_to_process = false;
			SPTDATA sp_message;
			{
				MutexGuard<Mutex> lock(s->exchange_point_.mutex_message_queue_);
				if (!s->exchange_point_.message_queue_.empty()) {
					sp_message = s->exchange_point_.message_queue_.top();
					s->exchange_point_.message_queue_.pop();
					has_message_to_process = true;
				}
			}
			if (!has_message_to_process) {
				SwitchToThread();
				continue;
			} else { 
				++messages_recieved;
				s->log_data(sp_message);
			}
		};
		return 0;
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
}

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

	Client* clients_threads[MAXIMUM_CLIENT_QUANTITY];
	for (uint32_t i = 0; i < clients_quantity; ++i) {
		uint8_t priority = rand() % (PRIORITY_LEVELS + 1);

		clients_threads[i] = new Client(exchange_point, priority, i+1);
		clients_threads[i]->do_work();
	}

	Server srv(exchange_point, clients_quantity, output_file);
	srv.do_work();

	for (uint32_t i = 0; i < clients_quantity; ++i) {
		clients_threads[i]->join();
		delete clients_threads[i];
	}
	srv.join();
	
	cout << "Program finished\n";
	cin.get();
	cin.get();

	output_file.clear();
	output_file.close();
	return EXIT_SUCCESS;
};