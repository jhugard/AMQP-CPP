// Test_AMQP-CPP.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <amqpcpp.h>

#include "WinsockConnectionHandler.h"
#include "TestMessages.pb.h"


int _tmain(int argc, _TCHAR* argv[])
{
	if (argc>5)
	{
		std::cout << "usage: " << argv[0] << " [<host> [<port> [<user> [<password>]]]]" << std::endl;
	}
	std::string host(argc >= 2 ? argv[1] : "localhost");
	std::string port(argc >= 3 ? argv[2] : DefaultRabbitMQPort);
	std::string user(argc >= 4 ? argv[3] : "guest");
	std::string pass(argc >= 5 ? argv[4] : "guest");

	// create an instance of your own connection handler
	WinsockConnectionHandler handler(host, port);

	// create an AMQP connection object
	AMQP::Connection connection(&handler, AMQP::Login(user.c_str(), pass.c_str()), "/");

	// and create a channel
	AMQP::Channel channel(&connection);

	// Set this to end the message loop
	bool bDone = false;

	// On error, output the error msg and end the loop
	channel.onError([&channel, &bDone](const char *msg){
		std::cerr << "Err: " << msg << std::endl;
		bDone = true;
	});

	// Wait for the channel to get ready (forces connecting to the host)
	channel
		.onReady([&channel, &bDone](){
			std::cout << "Channel is ready" << std::endl;
		});

	// use the channel object to call the AMQP method you like
	channel.declareExchange("my-exchange", AMQP::fanout);
	channel.declareQueue("my-queue");
	channel.bindQueue("my-exchange", "my-queue", "my-routingkey");

	// queue up an async protobuf message
	Test::Amqp::Hello msg;
	msg.set_from("RabbitMQ");
	msg.set_to("World!");
	std::string s(msg.SerializeAsString());
	channel.publish("my-exchange", "", s);

	// Get the message we just sent
	channel.get("my-queue")
		.onMessage([&channel, &bDone](const AMQP::Message &message, uint64_t deliveryTag, bool redelivered) {
			std::string s(message.body(), static_cast<unsigned int>(message.bodySize()));
			Test::Amqp::Hello msg;
			msg.ParseFromString(std::string(message.body(), static_cast<unsigned int>(message.bodySize())));
			std::cout << "Recv: Hello " << msg.to() << " from " << msg.from() << std::endl;

			//BUG? This ack does not remove the message from the queue
			channel.ack(deliveryTag);
			
			bDone = true;
			})
		.onError([&bDone](const char* msg) {
			std::cout << "Err: " << msg << std::endl;
			bDone = true;
			});

	// event loop
	while (!bDone)
	{
		// Process any data received on the connection
		if (handler.pump(&connection))
		{
			// Process any pending work on the channel
			channel.consume("my-queue");
		}
		// If none, sleep a while and try again
		else
		{
#if defined(_WIN32)
			::Sleep(50);
#else
			_sleep(50);
#endif
		}
	}

	return 0;
}

