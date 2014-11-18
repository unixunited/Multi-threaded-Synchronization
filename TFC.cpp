// ================================================ //
// File: TFC.cpp
// Author: Jordan Sparks
// COSC 4327 Operating Systems Lab 2, Dr. Burris
// ================================================ //
// Implements TFC (Task Force Command) class.
// ================================================ //

#include "TFC.hpp"
#include "Timer.hpp"
#include "resource.h"

// ================================================ //

const std::string TFC::Port = "27876";
bool TFC::RunArtificialProbes = true;

// ================================================ //

TFC::TFC(void) :
m_asteroids(),
m_mutex(1), m_empty(15), m_full(0),
m_probes(),
m_socket(INVALID_SOCKET),
m_fleetAlive(true),
m_inAsteroidField(false),
m_shields(5),
m_asteroidsDestroyed(0),
m_pClock(new Timer()),
m_guiEvents(),
m_scoutActive(false),
m_numPhaserProbesLaunched(0)
{
	int ret = this->init();
	printf("setupServer() = %d\n", ret);
}

// ================================================ //

TFC::~TFC(void)
{
	closesocket(m_socket);
}

// ================================================ //

int TFC::init(void)
{
	struct addrinfo* result = nullptr;
	struct addrinfo* ptr = nullptr;
	struct addrinfo hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve local address and port.
	int i = getaddrinfo(nullptr, TFC::Port.c_str(), &hints, &result);
	if (i != 0){
		return i;
	}

	// Create a socket for server to listen for connections.
	m_socket = socket(result->ai_family, 
					  result->ai_socktype, 
					  result->ai_protocol);
	if (m_socket == INVALID_SOCKET){
		freeaddrinfo(result);
		return i;
	}

	// Bind the socket to network address.
	i = bind(m_socket, result->ai_addr, static_cast<int>(result->ai_addrlen));
	if (i == SOCKET_ERROR){
		freeaddrinfo(result);
		closesocket(m_socket);
		return i;
	}

	// This is no longer needed, reclaim.
	freeaddrinfo(result);

	// Begin listening on socket for incoming connections.
	if (listen(m_socket, SOMAXCONN) == SOCKET_ERROR){
		closesocket(m_socket);
		return SOCKET_ERROR;
	}

	// Spawn a thread to accept new probe connections.
	std::thread t(&TFC::launchProbes, this);
	// Allow continued execution of calling thread.
	t.detach(); 

	return 0;
}

// ================================================ //

void TFC::reset(void)
{
	m_probes.clear();
	m_fleetAlive = true;
	m_shields = 5;
	m_asteroidsDestroyed = m_numPhaserProbesLaunched = 0;
	m_scoutActive = false;
	m_pClock->stop();
	TFC::RunArtificialProbes = false;
}

// ================================================ //

void TFC::launchProbes(void)
{
	while (true){
		// Accept incoming probe requests.
		struct sockaddr_in probeInfo = { 0 };
		int size = sizeof(probeInfo);
		SOCKET probeSocket = accept(m_socket, reinterpret_cast<struct sockaddr*>(&probeInfo), &size);
		if (probeSocket == INVALID_SOCKET){
			printf("TFC: accept() failed: %ld\n", WSAGetLastError());
			closesocket(probeSocket);
			continue;
		}

		// Store remote IP address and port.
		char* host = inet_ntoa(probeInfo.sin_addr);
		int port = ntohs(probeInfo.sin_port);
		IPAddress ip(host, port);

		// Receive the request.
		int r = 0;
		Probe::Message msg;
		r = recv(probeSocket, reinterpret_cast<char*>(&msg), sizeof(msg), 0);
		if (r > 0){
			if (m_inAsteroidField == false){
				if (msg.type == Probe::MessageType::LAUNCH_REQUEST){
					printf("Launch request received from %s:%d.\n", host, port);

					// Send a launch confirmation back to the probe, as well as the ID.
					static Uint probeIDCtr = 0;
					Probe::Message confirm;
					confirm.type = Probe::MessageType::CONFIRM_LAUNCH;
					confirm.id = probeIDCtr++;

					int s = send(probeSocket, reinterpret_cast<const char*>(&confirm), sizeof(confirm), 0);
					if (s > 0){
						printf("Sent launch confirmation, adding to probe list.\n");

						// Add probe to TFC list of probes.
						ProbeRecord probe;
						probe.socket = probeSocket;
						probe.id = confirm.id;
						probe.type = msg.LaunchRequest.type;
						if (probe.type == Probe::Type::PHASER){
							++m_numPhaserProbesLaunched;
						}
						probe.state = 0;
						m_probes.push_back(probe);

						// Spawn a thread to handle the new probe.
						std::thread t(&TFC::updateProbe, this, probe);
						t.detach();
					}
				}
			}
			// Don't allow new probe launches while navigating asteroid field.
			else{
				
			}
		}
		else{
			// Report error...
		}
	}
}

// ================================================ //

void TFC::updateProbe(const ProbeRecord& probe)
{
	printf("New updateProbe() thread with ID = %d\n", probe.id);
	bool probeAlive = true;

	while (m_fleetAlive && probeAlive){
		// Receive the request.
		if (m_inAsteroidField){
			// Only allow the scout probe to check destruction conditions.
			// This prevents possible race conditions in this step.
			if (probe.type == Probe::Type::SCOUT){
				// First, activate the scout probe if this is the first iteration.
				if (m_scoutActive == false){
					Probe::Message activate;
					activate.type = Probe::MessageType::SCOUT_REQUEST;
					int s = send(probe.socket, reinterpret_cast<const char*>(&activate), sizeof(activate), 0);
					if (s > 0){
						m_scoutActive = true;
					}
				}

				// If only the scout probe remains, or shields are gone, trigger fleet destruction.
				if (m_probes.size() == 1 || m_shields == 0){
					m_fleetAlive = m_inAsteroidField = false;
					Timer::Delay(1500);
					GUIEvent e;
					e.type = GUIEventType::FLEET_DESTROYED;
					m_guiEvents.push(e);
					// signal probes to terminate...
				}
				else if (m_asteroidsDestroyed > 55){
					m_inAsteroidField = false;
					Timer::Delay(1500);
					GUIEvent e;
					e.type = GUIEventType::FLEET_SURVIVED;
					m_guiEvents.push(e);
				}
			}

			int r = 0;
			Probe::Message msg;
			r = recv(probe.socket, reinterpret_cast<char*>(&msg), sizeof(msg), 0);
			if (r > 0){
				switch (msg.type){
				default:
					break;

				case Probe::MessageType::LAUNCH_REQUEST:
					// Only accept launch requests when not in asteroid field.					
					break;

				case Probe::MessageType::SCOUT_REQUEST:
					{
						Probe::Message response;
						// Ack request.
						response.type = Probe::MessageType::SCOUT_REQUEST;
						response.time = m_pClock->getTicks();
						int s = send(probe.socket, reinterpret_cast<const char*>(&response), sizeof(response), 0);
						if (s > 0){

						}
					}
					break;

				case Probe::MessageType::ASTEROID_FOUND:
					if (m_asteroids.full()){
						--m_shields;
						GUIEvent e;
						e.type = GUIEventType::SHIELDS_HIT;
						m_guiEvents.push(e);
						++m_asteroidsDestroyed;
					}
					else{
						// Wait for synchronized access to asteroid array.
						m_empty.wait();
						m_mutex.wait();

						if (m_asteroids.insert(msg.asteroid))
						{
							// Inform main GUI of new asteroid.
							GUIEvent e;
							e.type = GUIEventType::ASTEROID_FOUND;
							e.asteroid = msg.asteroid;
							m_guiEvents.push(e);
						}

						// Allow next person in.
						m_mutex.signal();
						m_full.signal();
					}
					break;

				case Probe::MessageType::DEFENSIVE_REQUEST:
					{
						Probe::Message response;
						if (m_asteroids.empty()){
							response.type = Probe::MessageType::NO_TARGET;
						}
						else{
							// Wait turn, prevent race conditions.							
							m_full.wait();
							m_mutex.wait();
							
							bool asteroidFound = false;
							// Keep retrieving the next asteroid until a valid
							// one is found.
							while (asteroidFound == false && m_asteroids.empty() == false){
								// Acquire next asteroid.
								Asteroid a = m_asteroids.remove();

								Uint time = m_pClock->getTicks();
								printf("------------\nCurrentTime: %d\nTimeFound: %d\nImpactTime: %d\n------------\n",
									   time, a.discoveryTime, a.impactTime);
								// See if there is time to destroy next asteroid.
								// [if (current time < time found + time to collision)]
								if (time < a.impactTime){
									// Send asteroid info to probe.
									response.asteroid = a;
									response.type = Probe::MessageType::TARGET_AVAILABLE;
									response.time = time;
									asteroidFound = true;
								}
								else{
									// Take hit on shields and report to GUI.
									--m_shields;
									GUIEvent e;
									e.type = GUIEventType::SHIELDS_HIT;
									m_guiEvents.push(e);									
								}

								// Trigger GUI event to remove asteroid from listview.
								GUIEvent e;
								e.type = GUIEventType::ASTEROID_REMOVED;
								e.x = a.id;
								m_guiEvents.push(e);
							}

							// Allow other probes to access asteroid queue.
							m_mutex.signal();
							m_empty.signal();
						}

						// Send the requested data to the probe.
						int s = send(probe.socket, reinterpret_cast<const char*>(&response), sizeof(response), 0);
						if (s > 0){

						}
					}
					break;

				case Probe::MessageType::TARGET_DESTROYED:
					printf("TARGET DESTROYED!\n");
					++m_asteroidsDestroyed;
					{
						GUIEvent e;
						e.type = GUIEventType::ASTEROID_DESTROYED;
						e.x = probe.id;
						m_guiEvents.push(e);
					}
					break;

				case Probe::MessageType::TERMINATED:
					{						
						++m_asteroidsDestroyed;
						// Trigger GUI event to remove probe.
						GUIEvent e;
						e.type = GUIEventType::PROBE_TERMINATED;
						e.x = probe.id;
						m_guiEvents.push(e);
						probeAlive = false;

						// Probe destroyed, remove from probe list.		
						for (std::vector<ProbeRecord>::iterator itr = m_probes.begin();
							 itr != m_probes.end();){
							if (itr->id == msg.id){
								itr = m_probes.erase(itr);
								break;
							}
							else{
								itr++;
							}
						}
					}
					break;
				}
			}
		}
		else{
			Timer::Delay(1);
		}		
	}

	closesocket(probe.socket);
}

// ================================================ //