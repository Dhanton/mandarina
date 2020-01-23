//Commands that can be received by the client
enum class ClientCommand {
    Null,
    Snapshot,
    InitialConditions
};

//Commands that can be received by the server
enum class ServerCommand {
    Null,
    PlayerReady,
    LatestSnapshotId,
    PlayerInput,
    ChangeInputRate,
    ChangeSnapshotRate
};