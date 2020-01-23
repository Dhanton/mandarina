#include "game_client.hpp"

#include "network_commands.hpp"
#include "helper.hpp"

GameClientCallbacks::GameClientCallbacks(GameClient* p)
{
    parent = p;
}

void GameClientCallbacks::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info)
{
    switch (info->m_info.m_eState)
    {
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        {
            if (info->m_eOldState == k_ESteamNetworkingConnectionState_Connecting) {
                parent->printMessage("Unable to reach server");

            } else if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer) {
                parent->printMessage("Connection with server closed by peer");

            } else {
                parent->printMessage("There was a problem with the connection");
            }

            parent->m_pInterface->CloseConnection(info->m_hConn, 0, nullptr, false);
            parent->m_serverConnectionId = k_HSteamNetConnection_Invalid;

            break;
        }

        case k_ESteamNetworkingConnectionState_Connecting:
        {
            parent->printMessage("Connecting...");
            break;
        }

        case k_ESteamNetworkingConnectionState_Connected:
        {
            parent->printMessage("Connection completed with server");
            parent->m_connected = true;

            //Eventually, sending ready command should be based on input from the player
            //Like interacting with something in the tavern
            //Or clicking a button in a menu

            CRCPacket packet;
            packet << (u8) ServerCommand::PlayerReady << true;
            parent->sendPacket(packet, parent->m_serverConnectionId, true);
            break;
        }
    }
}

GameClient::GameClient(const Context& context, const SteamNetworkingIPAddr& endpoint):
    m_gameClientCallbacks(this),
    InContext(context),
    NetPeer(&m_gameClientCallbacks, false),
    m_entityManager(context),
    m_tileMapRenderer(context, &m_tileMap)
{
    C_loadUnitsFromJson(context.jsonParser);

    m_context.renderTarget = &m_canvas;
    m_canvasCreated = false;

    m_entityManager.allocateAll();
    m_interSnapshot_it = m_snapshots.begin();
    m_requiredSnapshotsToRender = 3;

    m_currentInput.id = 1;
    m_freeView = false;
    m_initialZoom = 0.8f;
    m_currentZoom = m_initialZoom;

    ////////////////////// THIINGS TO LOAD FROM JSON FILE ////////////////////////
    m_updateRate = sf::seconds(1.f/30.f);
    m_inputRate = sf::seconds(1.f/30.f);

    //////////////////////////////////////////////////////////////////////////////

    if (!context.local) {
        m_serverConnectionId = connectToServer(endpoint);
        m_connected = false;

    } else {
        m_connected = true;
        m_serverConnectionId = context.localCon1;

        printMessage("Adding server in local connection");

        //local connection doesn't trigger callbacks
        CRCPacket packet;
        packet << (u8) ServerCommand::PlayerReady << true;
        sendPacket(packet, m_serverConnectionId, true);
    }
}

GameClient::~GameClient()
{
    m_pInterface->CloseConnection(m_serverConnectionId, 0, nullptr, false);
}

void GameClient::mainLoop(bool& running)
{
    sf::RenderWindow window{{960, 640}, "Mandarina Prototype", sf::Style::Fullscreen};
    // sf::RenderWindow window{{960, 640}, "Mandarina Prototype", sf::Style::Titlebar | sf::Style::Close};
    sf::View view = window.getDefaultView();
    view.zoom(m_initialZoom);

    m_context.window = &window;
    m_context.view = &view;

    sf::Clock clock;

    sf::Time updateTimer;
    sf::Time inputTimer;

    bool focused = true;

    while (running) {
        sf::Time eTime = clock.restart();
        
        m_worldTime += eTime;

        updateTimer += eTime;
        inputTimer += eTime;

        receiveLoop();

        while (inputTimer >= m_inputRate) {
            sf::Event event;

            while (window.pollEvent(event)) {
                if (event.type == sf::Event::GainedFocus) {
                    focused = true;
                }

                if (event.type == sf::Event::LostFocus) {
                    focused = false;
                }
                
                if (event.type == sf::Event::Closed) {
                    running = false;
                }

                if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
                    running = false;
                }

                handleInput(event, focused);
            }

            saveCurrentInput();
            inputTimer -= m_inputRate;
        }

        while (updateTimer >= m_inputRate) {
            update(m_inputRate);
            updateTimer -= m_inputRate;
        }
        
        renderUpdate(eTime);

        window.clear();
        window.setView(view);

        if (m_canvasCreated) {
            //draw everything to the canvas
            m_canvas.clear();

            m_tileMapRenderer.renderBeforeEntities(m_canvas);
            m_canvas.draw(m_entityManager);
            m_tileMapRenderer.renderAfterEntities(m_canvas);

            m_canvas.display();

            //draw the canvas to the window
            sf::Sprite sprite(m_canvas.getTexture());
            window.draw(sprite);
        }

        window.display();
    }
}

void GameClient::receiveLoop()
{
    NetPeer::receiveLoop(m_serverConnectionId);
}

void GameClient::update(sf::Time eTime)
{
    if (!m_connected) return;

    m_infoTimer += eTime;

    if (m_infoTimer >= sf::seconds(5.f)) {
        SteamNetworkingQuickConnectionStatus status;
        SteamNetworkingSockets()->GetQuickConnectionStatus(m_serverConnectionId, &status);

        printMessage("-------------------------");
        printMessage("Ping: %d", status.m_nPing);
        printMessage("In/s: %f", status.m_flInBytesPerSec);
        printMessage("Out/s: %f", status.m_flOutBytesPerSec);

        m_infoTimer = sf::Time::Zero;
    }

    m_entityManager.update(eTime);

    CRCPacket outPacket;
    writeLatestSnapshotId(outPacket);

    sendPacket(outPacket, m_serverConnectionId, false);
}

void GameClient::renderUpdate(sf::Time eTime)
{
    C_Unit* unit = m_entityManager.units.atUniqueId(m_entityManager.controlledEntityUniqueId);

#ifdef MANDARINA_DEBUG
    if (m_freeView) {
        Vector2 cameraPos = m_context.view->getCenter();
        PlayerInput_applyInput(m_cameraInput, cameraPos, 500.f, eTime);
        m_context.view->setCenter(cameraPos);

    } else {
#endif
    
    Vector2 pos;
    if (unit) {
        pos = unit->pos;
    } else {
        pos = m_latestControlledPos;
    }

    //@WIP: Center camera around controlled character (or last position where it was)
    //Don't move the camera outside of the map
    //Do it smoothly (like nuclear throne does)
    //interpolate camera between old positions

    Vector2 viewSize = m_context.view->getSize();

    //don't let the camera move outside the map
    pos.x = Helper_clamp(pos.x, viewSize.x/2.f, (float) m_tileMapRenderer.getTotalSize().x - viewSize.x/2.f);
    pos.y = Helper_clamp(pos.y, viewSize.y/2.f, (float) m_tileMapRenderer.getTotalSize().y - viewSize.y/2.f);

    m_context.view->setCenter(pos);

#ifdef MANDARINA_DEBUG
    }
#endif

    if (std::next(m_interSnapshot_it, m_requiredSnapshotsToRender) != m_snapshots.end()) {
        m_interElapsed += eTime;

        auto next_it = std::next(m_interSnapshot_it);

        sf::Time totalTime = next_it->worldTime - m_interSnapshot_it->worldTime;

        if (m_interElapsed >= totalTime && std::next(next_it) != m_snapshots.end()) {
            m_interElapsed -= totalTime;

            m_interSnapshot_it = std::next(m_interSnapshot_it);
            next_it = std::next(next_it);

            setupNextInterpolation();

            totalTime = next_it->worldTime - m_interSnapshot_it->worldTime;
        }

        m_entityManager.performInterpolation(&m_interSnapshot_it->entityManager, &next_it->entityManager, 
                                             m_interElapsed.asSeconds(), totalTime.asSeconds());

        //interpolate the controlled entity between the latest two inputs
        if (unit) {
            if (!m_inputSnapshots.empty()) {
                m_controlledEntityInterTimer += eTime;

                //interpolate using current entity position if there's only one input available
                Vector2 oldPos = unit->pos;
                float oldAimAngle = unit->aimAngle;

                auto oldIt = std::next(m_inputSnapshots.end(), -2);

                if (oldIt != m_inputSnapshots.end()) {
                    oldPos = oldIt->endPosition;
                    oldAimAngle = oldIt->input.aimAngle;
                }

                unit->pos = Helper_lerpVec2(oldPos, m_inputSnapshots.back().endPosition, 
                                            m_controlledEntityInterTimer.asSeconds(), m_inputRate.asSeconds());

                m_latestControlledPos = unit->pos;                                            
            }

            Vector2 mousePos = static_cast<Vector2>(sf::Mouse::getPosition(*m_context.window));
            Vector2 viewPos = m_context.view->getCenter() - m_context.view->getSize()/2.f;

            //transform the mouse position to world coordinates
            //(this calculation transform window coordinates to world coordinates)
            mousePos *= m_currentZoom;
            mousePos += viewPos;

            PlayerInput_updateAimAngle(m_currentInput, unit->pos, mousePos);
            unit->aimAngle = m_currentInput.aimAngle;
        }
    }
}

void GameClient::setupNextInterpolation()
{
    m_entityManager.copySnapshotData(&m_interSnapshot_it->entityManager);

    //we need the end position of controlled entity in the server for this snapshot
    C_Unit* snapshotUnit =  m_interSnapshot_it->entityManager.units.atUniqueId(m_interSnapshot_it->entityManager.controlledEntityUniqueId);
    C_Unit* controlledUnit = m_entityManager.units.atUniqueId(m_entityManager.controlledEntityUniqueId);

    if (snapshotUnit && controlledUnit) {
        checkServerInput(m_interSnapshot_it->latestAppliedInput, snapshotUnit->pos, controlledUnit->movementSpeed);
    }
}

void GameClient::handleInput(const sf::Event& event, bool focused)
{
    if (!m_connected) return;

    if (focused) {
#ifdef MANDARINA_DEBUG
        //render collision shapes
        if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F1) {
            m_entityManager.renderingDebug = !m_entityManager.renderingDebug;
        }

        //free view
        if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F2) {
            m_freeView = !m_freeView;

            //clear the previously used input
            if (m_freeView) {
                PlayerInput_clearKeys(m_currentInput);
            } else {
                PlayerInput_clearKeys(m_cameraInput);

                //reset the zoom
                m_context.view->zoom(m_initialZoom/m_currentZoom);
                m_currentZoom = m_initialZoom;
            }
        }

        if (m_freeView) {
            //zoom
            if (event.type == sf::Event::MouseWheelScrolled) {
                float zoom = event.mouseWheelScroll.delta == -1.f ? 2.f : 1/2.f;
                m_currentZoom *= zoom;
                m_context.view->zoom(zoom);
            }      
        }
#endif

        if (m_freeView) {
            PlayerInput_handleKeyboardInput(m_cameraInput, event);
        } else {
            PlayerInput_handleKeyboardInput(m_currentInput, event);
        }

    } else {
        PlayerInput_clearKeys(m_currentInput);
    }
}

void GameClient::saveCurrentInput()
{
    C_Unit* unit = m_entityManager.units.atUniqueId(m_entityManager.controlledEntityUniqueId);

    //@TODO: Should we send inputs even if there's no entity
    //to ensure players can move the entity as soon as available?
    if (!unit) return;

    //we don't want to modify entity position just yet (we want to interpolate it smoothly)
    Vector2 unitPos = unit->pos;

    //apply input using the previous input position if possible
    if (!m_inputSnapshots.empty()) {
        unitPos = m_inputSnapshots.back().endPosition;
    }

    C_EntityManager* manager = &m_entityManager;

    //use the most recent EntityManager available
    if (!m_snapshots.empty()) {
        manager = &m_snapshots.back().entityManager;
    }

    //we dont modify the unit since we intepolate its position
    //between two inputs (result is stored in unitPos)
    C_Unit_applyInput(*unit, unitPos, m_currentInput, C_ManagersContext(manager), m_inputRate);

    //send this input
    {
        CRCPacket outPacket;
        outPacket << (u8) ServerCommand::PlayerInput;
        PlayerInput_packData(m_currentInput, outPacket);
        sendPacket(outPacket, m_serverConnectionId, false);
    }

    m_inputSnapshots.push_back(InputSnapshot());
    m_inputSnapshots.back().input = m_currentInput;
    m_inputSnapshots.back().endPosition = unitPos;

    //reset input timer
    m_currentInput.timeApplied = sf::Time::Zero;
    m_controlledEntityInterTimer = sf::Time::Zero;

    m_currentInput.id++;
}

void GameClient::checkServerInput(u32 inputId, const Vector2& endPosition, u16 movementSpeed)
{
    if (inputId == 0) {
        if (!m_inputSnapshots.empty()) {
            m_inputSnapshots.back().endPosition = endPosition;
        }

        return;
    }

    auto it = m_inputSnapshots.begin();

    //find the snapshot corresponding to inputId (while also deleting older ids)
    while (it != m_inputSnapshots.end()) {
        //we want to leave at least two inputs to interpolate properly
        if (it->input.id < inputId && m_inputSnapshots.size() > 2) {
            it = m_inputSnapshots.erase(it);
        } else {
            if (it->input.id == inputId) {
                break;
            }

            it = std::next(it);
        }
    }

    //if there's no input, that means it was already checked
    if (it == m_inputSnapshots.end()) return;

    Vector2 predictedEndPos = it->endPosition;

    //we want to leave at least two inputs to interpolate properly
    if (m_inputSnapshots.size() > 2) {
        it = m_inputSnapshots.erase(it);
    }

    //correct wrong predictions
    if (predictedEndPos != endPosition) {

#if 0 && defined MANDARINA_DEBUG
        //this message can get annoying because there are a lot of minor prediction errors
        printMessage("Incorrect prediction - Delta: %f", Helper_vec2length(predictedEndPos - endPosition));
#endif

        Vector2 newPos = endPosition;

        // recalculate all the positions of all the inputs starting from this one
        while (it != m_inputSnapshots.end()) {
            PlayerInput_repeatAppliedInput(it->input, newPos, movementSpeed);

            //try to smoothly correct the input one step at a time
            //(this weird method is the one that gets the best results apparently)
            Vector2 dirVec = newPos - it->endPosition;
            float distance = Helper_vec2length(dirVec);

            //@TODO: These values (0.5, 10, 250) have to be tinkered to make it look as smooth as possible
            //The method used could also change if this one's not good enough
            if (distance < 250.f) {
                float offset = std::max(0.5, Helper_lerp(0.0, 10.0, distance, 250.0));
                it->endPosition += Helper_vec2unitary(dirVec) * std::min(offset, distance);

            } else {
                it->endPosition = newPos;
            }

            it = std::next(it);
        }
    }
}

void GameClient::processPacket(HSteamNetConnection connectionId, CRCPacket& packet)
{
    while (!packet.endOfPacket()) {
        u8 command = 0;
        packet >> command;

        handleCommand(command, packet);
    }
}

void GameClient::handleCommand(u8 command, CRCPacket& packet)
{
    switch (ClientCommand(command))
    {
        case ClientCommand::Null:
        {
            printMessage("handleCommand error - NULL command");

            //receiving null command invalidates the rest of the packet
            packet.clear();
            break;
        }

        case ClientCommand::Snapshot:
        {
            u32 snapshotId, prevSnapshotId;
            packet >> snapshotId >> prevSnapshotId;

            u32 appliedPlayerInputId;
            packet >> appliedPlayerInputId;

            u32 controlledEntityUniqueId;
            packet >> controlledEntityUniqueId;

            Snapshot* prevSnapshot = findSnapshotById(prevSnapshotId);
            C_EntityManager* prevEntityManager = nullptr;

            if (!prevSnapshot) {
                if (prevSnapshotId != 0) {
                    printMessage("Snapshot error - Previous snapshot doesn't exist");
                    packet.clear();
                    break;

                }

            } else {
                prevEntityManager = &prevSnapshot->entityManager;
            }

            m_snapshots.emplace_back();

            Snapshot& snapshot = m_snapshots.back();
            snapshot.id = snapshotId;
            snapshot.entityManager.loadFromData(prevEntityManager, packet);
            snapshot.worldTime = m_worldTime;
            snapshot.latestAppliedInput = appliedPlayerInputId;
            snapshot.entityManager.controlledEntityUniqueId = controlledEntityUniqueId;

            removeOldSnapshots(prevSnapshotId);

            //populate C_EntityManager if we had no previous snapshots
            //(this happens when we receive the first snapshot)
            if (m_snapshots.size() == 1) {
                m_interSnapshot_it = m_snapshots.begin();
                setupNextInterpolation();
            }

            //send latest snapshot id
            CRCPacket outPacket;
            writeLatestSnapshotId(outPacket);
            sendPacket(outPacket, m_serverConnectionId, false);

            break;
        }

        case ClientCommand::InitialConditions:
        {
            std::string filename;
            packet >> filename;

            m_tileMap.loadFromFile(MAPS_PATH + filename + "." + MAP_FILENAME_EXT);
            m_tileMapRenderer.generateLayers();

            m_canvas.create(m_tileMapRenderer.getTotalSize().x, m_tileMapRenderer.getTotalSize().y);
            m_canvasCreated = true;

            packet >> m_entityManager.controlledEntityUniqueId;

            break;
        }
    }
}

void GameClient::removeOldSnapshots(u32 olderThan)
{
    auto it = m_snapshots.begin();

    //only erase elements that no longer need to be rendered
    while(it != m_interSnapshot_it) {
        if (it->id < olderThan) {
            it = m_snapshots.erase(it);
        } else {
            it = std::next(it);
        }
    }
}

GameClient::Snapshot* GameClient::findSnapshotById(u32 snapshotId)
{
    for (Snapshot& snapshot : m_snapshots) {
        if (snapshot.id == snapshotId) {
            return &snapshot;
        }
    }

    return nullptr;
}

void GameClient::writeLatestSnapshotId(CRCPacket& packet)
{
    packet << (u8) ServerCommand::LatestSnapshotId;
    packet << m_snapshots.back().id;
}

