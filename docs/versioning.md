# Overview

This document describes the various mechanisms used to keep the overall system working as it evolves.

# Ledger versioning
## protocolVersion
This uint32 describes the version number of the overall protocol.
Protocol in this case is defined both as "wire format" (ie, the serialized forms of all objects stored in the ledger) and its behavior.

This version number is incremented every time the protocol changes.


### Integration with consensus
Each node has a "minProtocolVersion" and "maxProtocolVersion" set in their config file used to validate version numbers for building new ledgers (they will reject versions outside of the range); "maxProtocolVersion" is the latest version of the protocol that the node implements.

Each node advertises their supported min-max protocolVersion alongside their quorum slices so that network operators can evaluate risk when increasing "minProtocolVersion" on their nodes as it may be a threat to the network's ability to reach consensus (loss of liveness).

Consensus is reached on a given transaction set, previous ledger and protocolVersion.

After consensus is reached, protocolVersion is saved in the resulting LedgerHeader.

### minProtocolVersion vs history
minProtocolVersion is distinct from the version an instance understands:
typically an implementation understands versions n .. maxProtocolVersion, where n <= minProtocolVersion.
The reason for this is that nodes must be able to replay transactions from history (down to version 'n'), yet there might be some issue/vulnerability that we don't want to be exploitable for new transactions.


## Ledger object versioning

Data structures that are likely to evolve over time contain the following extension point:
```C++
    union switch(int v)
    {
    case 0:
        void;
    } ext;
```

The version 'v' in this case refers to the version of the object and permits the addition of new arms.

This scheme offers several benefits:
* implementations become wire compatible without code changes only by updating their protocol definition files
* even without updating the protocol definition files, older implementations continue to function as long as they don't encounter newer formats
* promotes code sharing between versions of the objects

note that while this scheme promotes code sharing for components consuming those objects, this is not necessarily true for core itself as the behavior has to be preserved for all versions: in order to reconstruct the ledger chain from arbitrary points in time, the behavior has to be 100% compatible.

## Operations versioning

Operations are versioned as a whole: if a new parameter needs to be added or changed, versioning is achieved by adding a new operation.
This causes some duplication of logic in client but avoids introducing potential bugs in clients. For example: code that would sign only certain types of transactions have to be fully aware of what they are signing.

## Envelope versioning

The pattern used to allow for extensibility of envelopes (signed content) is
```C++
union TransactionEnvelope switch (int v)
{
case 0:
    struct
    {
        Transaction tx;
        DecoratedSignature signatures<20>;
    } v0;
};
```

This allows to both have the capability to modify the envelope if needed while enforcing that clients don't blindly consume content that they could not validate.

## Upgrading objects that don't have an extension point

The object's schema has to be cloned and its parent object has to be updated to use the new object type. The assumption here is that there is no unversioned "root" object.

## Supported implementations lifetime considerations

In order to keep the code base in a maintainable state, implementations may not preserve the ability to playback from genesis and instead opt to support a limited range, for example only preserve the capability to replay the previous 3 months of transactions (assuming that the network's minProtocolVersion is more recent than this).
This does not change the ability for the node to (re)join or participate in the network; it only effects the ability for a node to do historical validation.

# Overlay versioning

Overlay follows a similar pattern for versioning: it has a min-maxOverlayVersion.

The versioning policy at the overlay layer is a lot more aggressive when it comes to the deprecation schedule as the set of nodes involved is limited to the ones that connect directly to the instance.

With this in mind, structures follow the "clone" model at this layer:
if a message needs to be modified, a new message is defined by cloning the old message type using a new type identifier.
The advantage of the clone model is that it makes it possible to refactor large parts of the code, knowing that the older implementation will be deleted anyways (and therefore avoiding the headache of maintaining older versions).
Also, at this layer, it is acceptable to modify the behavior of older versions as long as it stays compatible.
The implementation may decide to share the underlying code (by converting legacy messages into the new format internally for example).

The "HELLO" message exchanged when peers connect to each other contains the min and max version the instance supports, the other endpoint may decide to disconnect right away if it's not compatible.

