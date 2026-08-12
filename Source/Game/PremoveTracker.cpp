#include "PremoveTracker.h"

#include <utility>

void PremoveTracker::Update(std::string expectedOwnMove, std::string predictedOpponentMove, std::string ourResponse, std::uint64_t generation)
{
    std::scoped_lock lock(m_Mutex);
    m_Candidate = Candidate{std::move(expectedOwnMove), std::move(predictedOpponentMove), std::move(ourResponse), generation};
}

std::optional<PremoveTracker::Candidate> PremoveTracker::Peek() const
{
    std::scoped_lock lock(m_Mutex);
    return m_Candidate;
}

std::optional<PremoveTracker::Candidate> PremoveTracker::Take()
{
    std::scoped_lock lock(m_Mutex);
    std::optional<Candidate> taken = std::move(m_Candidate);
    m_Candidate.reset();
    return taken;
}

void PremoveTracker::InvalidateIfMismatched(const std::string& expectedOwnMove)
{
    std::scoped_lock lock(m_Mutex);
    if (m_Candidate && m_Candidate->ExpectedOwnMove != expectedOwnMove)
        m_Candidate.reset();
}
