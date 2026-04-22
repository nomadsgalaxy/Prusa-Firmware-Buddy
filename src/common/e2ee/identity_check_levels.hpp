#pragma once

namespace e2ee {
enum class IdentityCheckLevel {
    KnownOnly, // Abort if the identity is unknown
    Ask, // Ask about unknown identity, the user can add it to trusted permanently, or temporarily for this pritn only, or abort
    AnyIdentity, // don't care about identities at all, print anything
};
}
