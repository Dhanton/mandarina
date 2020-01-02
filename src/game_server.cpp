#include "game_server.hpp"

#include <SFML/System/Clock.hpp>
#include <csignal>

#include "network_commands.hpp"
#include "player_input.hpp"

GameServerCallbacks::GameServerCallbacks(GameServer* p)
{
    parent = p;
}

void GameServerCallbacks::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info)
{
    switch (info->m_info.m_eState)
    {
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        {
            int index = parent->getIndexByConnectionId(info->m_hConn);

            if (index != -1) {
                u32 clientId = parent->m_clients[index].clientId;

                if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer) {
                    parent->printMessage("Connection with client %d closed by peer", clientId);

                } else {
                    parent->printMessage("Connection with client %d lost", clientId);
                }

                parent->m_clients.removeElement(clientId);
                parent->m_pInterface->CloseConnection(info->m_hConn, 0, nullptr, false);
            }

            break;
        }

        case k_ESteamNetworkingConnectionState_Connecting:
        {
            int index = -1;
            
            if (parent->m_gameStarted) {
                //traverse all clients
                //if one of them is not valid
                //and has the same identity (secret key)
                //then that one is reconnecting
            } else {
                index = parent->addClient();
            }

            if (index != -1) {
                parent->m_clients[index].connectionId = info->m_hConn;

                u32 clientId = parent->m_clients[index].clientId;

                //Set the rest of the parameters (displayName, character, team, etc)

                if (parent->m_pInterface->AcceptConnection(info->m_hConn) != k_EResultOK) {
                    parent->printMessage("There was an error accepting connection with client %d", clientId);
                    parent->m_pInterface->CloseConnection(info->m_hConn, 0, nullptr, false);
                    break;
                }

                if (!parent->addClientToPoll(index)) {
                    parent->printMessage("There was an error adding client %d to poll", clientId);
                    parent->m_pInterface->CloseConnection(info->m_hConn, 0, nullptr, false);
                    break;
                }

                parent->printMessage("Connecting with client %d", clientId);
            }

            break;
        }

        case k_ESteamNetworkingConnectionState_Connected:
        {
            parent->onConnectionCompleted(info->m_hConn);

            break;
        }
    }
}

bool GameServer::SIGNAL_SHUTDOWN = false;

GameServer::GameServer(const Context& context, int partyNumber):
    m_gameServerCallbacks(this),
    InContext(context),
    NetPeer(&m_gameServerCallbacks, true),

    //TODO: Initial size should be the expected value of the amount of clients that will try to connect
    //This can be hight/low independently of partyNumber 
    //for example if a streamer sets up a server maybe 10k people try to connect (even for a 2-man party)
    //Still has to always be larger than partyNumber
    INITIAL_CLIENTS_SIZE(partyNumber * 2 + 10)
{
    m_gameStarted = false;
    m_partyNumber = partyNumber;
    m_lastClientId = 0;
    m_lastSnapshotId = 0;

    //Resize this vector to avoid dynamically adding elements
    //(this will still happen if more clients connect)
    m_clients.resize(INITIAL_CLIENTS_SIZE);

    m_entityManager.allocateAll();
    
    m_entityManager.createEntity(EntityType::TEST_CHARACTER, Vector2(300.f, 150.f));
    m_entityManager.createEntity(EntityType::TEST_CHARACTER, Vector2(500.f, 300.f));
    m_entityManager.createEntity(EntityType::TEST_CHARACTER, Vector2(600.f, 200.f));

    if (!context.local) {
        m_endpoint.ParseString("127.0.0.1:7000");
        m_pollId = createListenSocket(m_endpoint);

    } else {
        m_isServer = false;
        m_pollId.listenSocket = context.localCon2;
        
        int index = addClient();
        m_clients[index].connectionId = context.localCon2;

        printMessage("Adding client in local connection");

        onConnectionCompleted(context.localCon2);
    }

#ifndef _WIN32
    //Shutdown when Ctrl+C on linux
    struct sigaction sigIntHandler;

   sigIntHandler.sa_handler = [] (int s) -> void {SIGNAL_SHUTDOWN = true;};
   sigemptyset(&sigIntHandler.sa_mask);
   sigIntHandler.sa_flags = 0;

   sigaction(SIGINT, &sigIntHandler, NULL);
#endif // _WIN32
}

GameServer::~GameServer()
{
    for (int i = 0; i < m_clients.firstInvalidIndex(); ++i) {
        m_pInterface->CloseConnection(m_clients[i].connectionId, 0, nullptr, false);
    }

    m_pInterface->CloseListenSocket(m_pollId.listenSocket);
    m_pInterface->DestroyPollGroup(m_pollId.pollGroup);
}

void GameServer::mainLoop(bool& running)
{
    sf::Clock clock;

    //@TODO: Load this from json config file
    //**see how valve does it for counter strike, in regards to update/snapshot/input rates config**
    const sf::Time updateSpeed = sf::seconds(1.f/30.f);
    const sf::Time snapshotSpeed = sf::seconds(1.f/20.f);

    sf::Time updateTimer;
    sf::Time snapshotTimer;

    while (running) {
        sf::Time eTime = clock.restart();

        updateTimer += eTime;
        snapshotTimer += eTime;

        receiveLoop();

        if (updateTimer >= updateSpeed) {
            update(updateSpeed, running);
            updateTimer -= updateSpeed;
        }

        if (snapshotTimer >= snapshotSpeed) {
            sendSnapshots();
            snapshotTimer -= snapshotSpeed;
        }

        //Remove this for maximum performance (more CPU usage)
        // std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void GameServer::receiveLoop()
{
    if (m_context.local) {
        NetPeer::receiveLoop(m_pollId.listenSocket);
    } else {
        NetPeer::receiveLoop(m_pollId.pollGroup);
    }
}

void GameServer::update(const sf::Time& eTime, bool& running)
{
#ifndef _WIN32
    if (SIGNAL_SHUTDOWN) {
        running = false;
        std::cout << std::endl;
        return;
    }
#endif //_WIN32

    if (!m_gameStarted) {
        int playersReady = 0;

        for (int i = 0; i < m_clients.firstInvalidIndex(); ++i) {
            if (m_clients[i].isReady) {
                playersReady++;
            }
        }

        if (playersReady >= m_partyNumber) {
            m_gameStarted = true;

            printMessage("Game starting");

            //remove valid clients that didn't make it into the party
            for (int i = m_partyNumber; i < m_clients.firstInvalidIndex(); ++i) {
                m_pInterface->CloseConnection(m_clients[i].connectionId, 0, nullptr, false);
                m_clients.removeElement(m_clients[i].clientId);
            }

            //to save space, since no new clients are gonna be added
            m_clients.removeInvalidData();

            //inform the players that the game is starting
            //start the main game level
        }
    }

    m_worldTime += eTime;

    /////////////////////////////////////////////////////////////////////////////

    //Important to note that even if the game hasn't started yet
    //the idea is to be able to walk around and do a bunch of activities
    //with your character

    //So the game "lobby" is like a tavern for players to gather before the game
    //Players can perform an action to inform the server that they're ready
    
    //When all party members are ready the game starts 
    //All the other players are kicked out

    m_entityManager.update(eTime);
}

void GameServer::sendSnapshots()
{
    //will store oldest snapshot used by clients
    u32 oldestSnapshotId = -1;

    //Take current snapshot
    u32 snapshotId = ++m_lastSnapshotId;
    Snapshot& snapshot = m_snapshots.emplace(snapshotId, Snapshot()).first->second;

    snapshot.worldTime = m_worldTime;
    snapshot.id = snapshotId;

    m_entityManager.takeSnapshot(&snapshot.entityManager);

    for (int i = 0; i < m_clients.firstInvalidIndex(); ++i) {
        CRCPacket outPacket;
        outPacket << (u8) ClientCommand::Snapshot;
        outPacket << m_lastSnapshotId;
        outPacket << m_clients[i].snapshotId;
        outPacket << m_clients[i].latestInputId;

        //@TODO: See if this is the best way to send this
        outPacket << m_clients[i].controlledEntityUniqueId;

        if (m_clients[i].snapshotId < oldestSnapshotId) {
            oldestSnapshotId = m_clients[i].snapshotId;
        }

        auto it = m_snapshots.find(m_clients[i].snapshotId);
        EntityManager* snapshotManager = nullptr;

        if (it != m_snapshots.end()) {
            snapshotManager = &it->second.entityManager;
        }

        m_entityManager.packData(snapshotManager, outPacket);

        sendPacket(outPacket, m_clients[i].connectionId, false);
    }

    //snapshots no longer needed are deleted
    auto it = m_snapshots.begin();
    while (it != m_snapshots.end()) {
        if (it->first < oldestSnapshotId) {
            it = m_snapshots.erase(it);
        } else {
            it = std::next(it);
        }
    }
}

void GameServer::processPacket(HSteamNetConnection connectionId, CRCPacket& packet)
{
    int index = getIndexByConnectionId(connectionId);

    if (index == -1) {
        printMessage("processPacket error - Invalid connection id %d", connectionId);
        return;
    }

    //@TODO: Check packets are not super big to avoid
    //attackers slowing down the server with the proccessing of big packets
    //packets from clients are supposed to be very small (only inputs and snapshotId)
    // if (packet.getDataSize() > 20000) {
    //     display some message;
    //     ??disconnect the client??
    //     return;
    // }

    while (!packet.endOfPacket()) {
        u8 command = 0;
        packet >> command;

        handleCommand(command, index, packet);
    }
}

void GameServer::handleCommand(u8 command, int index, CRCPacket& packet)
{
    switch (ServerCommand(command))
    {
        case ServerCommand::Null:
        {
            printMessage("handleCommand error - NULL command (client %d)", m_clients[index].clientId);

            //receiving null command invalidates the rest of the packet
            packet.clear();
            break;
        }

        case ServerCommand::PlayerReady:
        {
            bool ready;
            packet >> ready;

            m_clients[index].isReady = ready;
            break;
        }

        case ServerCommand::LatestSnapshotId:
        {
            u32 latestId;
            packet >> latestId;

            if (latestId <= m_lastSnapshotId && latestId >= m_clients[index].snapshotId) {
                m_clients[index].snapshotId = latestId;
            }

            break;
        }

        case ServerCommand::PlayerInput:
        {
            PlayerInput playerInput;

            u8 inputNumber;
            packet >> inputNumber;

            if (inputNumber == 0) break;

            if (inputNumber > 4) {
                printMessage("PlayerInput error - Too many inputs (%d)", inputNumber);
                packet.clear();

                break;
            }

            TestCharacter* entity = m_entityManager.m_characters.atUniqueId(m_clients[index].controlledEntityUniqueId);

            //we still have to load the data even if the entity doesn't exist
            //@EXPLOIT: Players can send any number of inputs as fast as possible with different commands

            for (int i = 0; i < inputNumber; ++i) {
                PlayerInput_loadFromData(playerInput, packet);

                //@WIP: Time applied is the time the client has set up
                //(don't send it over the network anymore)

                //we trust the client as long as the applied time stays within reasonable values
                playerInput.timeApplied = std::min(playerInput.timeApplied, sf::seconds(0.1));

                //only apply inputs that haven't been applied yet
                if (entity && playerInput.id > m_clients[index].latestInputId) {
                    TestCharacter_applyInput(*entity, playerInput);

                    m_clients[index].latestInputId = playerInput.id;
                }
            }

            break;
        }

        case ServerCommand::InputRate:
        {
            //@WIP: Modify the client's input rate here

            //by default all clients start with the same rate (1/30)
            //clients can turn it up and down with limits [1/20, ..., serverConfig.maxInputRate]

            //inputs are stored in a queue and applied before each update
            //queue has a size limit (corresponding to the client inputRate and the server updateRate)

            break;
        }
    }
}

void GameServer::onConnectionCompleted(HSteamNetConnection connectionId)
{
    int index = getIndexByConnectionId(connectionId);

    if (index != -1) {
        //@TODO: Create based on game mode settings (position, teamId) and hero selection (class type)

        int entityIndex = m_entityManager.createEntity(EntityType::TEST_CHARACTER, Vector2(100.f, 100.f));
        TestCharacter* entity = m_entityManager.m_characters.atIndex(entityIndex);

        if (entity) {
            m_clients[index].controlledEntityUniqueId = entity->uniqueId;
            entity->teamId = m_clients[index].teamId;
        }

        printMessage("Connection completed with client %d", m_clients[index].clientId);
    }
}

int GameServer::addClient()
{
    u32 clientId = ++m_lastClientId;
    int index = m_clients.addElement(clientId);

    m_clients[index].clientId = clientId;

    return index;
}

bool GameServer::addClientToPoll(int index)
{
    if (!m_clients.isIndexValid(index)) {
        printMessage("addToPoll error - Invalid index %d", index);
        return false;
    }

    return m_pInterface->SetConnectionPollGroup(m_clients[index].connectionId, m_pollId.pollGroup);
}

int GameServer::getIndexByConnectionId(HSteamNetConnection connectionId) const
{
    for (int i = 0; i < m_clients.firstInvalidIndex(); ++i) {
        if (m_clients[i].connectionId == connectionId) {
            return i;
        }
    }

    return -1;
}
