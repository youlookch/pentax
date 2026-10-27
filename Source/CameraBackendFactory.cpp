#include "CameraBackend.h"
#include "GPhoto2Backend.h"

#if PENTAX_WITH_RICOH_SDK
// Défini dans RicohSdkBackend.cpp (compilé uniquement avec le SDK).
std::unique_ptr<CameraBackend> createRicohSdkBackend();
#endif

std::unique_ptr<CameraBackend> CameraBackend::createBest()
{
   #if PENTAX_WITH_RICOH_SDK
    // Couche de communication officielle Ricoh : le même protocole USB/PTP
    // propriétaire qu'Image Transmitter 2.
    return createRicohSdkBackend();
   #else
    return std::make_unique<GPhoto2Backend>();
   #endif
}
