#include <PCH.h>
#include <Foundation/Memory/CommonAllocators.h>
#include <Foundation/Memory/LargeBlockAllocator.h>

struct NonAlignedVector
{
  EZ_DECLARE_POD_TYPE();

  NonAlignedVector()
  {
    x = 5.0f;
    y = 6.0f;
    z = 8.0f;
  }

  float x;
  float y;
  float z;
};

struct EZ_ALIGN_16(AlignedVector)
{
  EZ_DECLARE_POD_TYPE();

  AlignedVector()
  {
    x = 5.0f;
    y = 6.0f;
    z = 8.0f;
  }

  float x;
  float y;
  float z;
  float w;
};

template <typename T>
void TestAlignmentHelper(size_t uiExpectedAlignment)
{
  ezIAllocator* pAllocator = ezFoundation::GetAlignedAllocator();
  EZ_TEST(pAllocator != NULL);

  size_t uiAlignment = EZ_ALIGNMENT_OF(T);
  EZ_TEST(uiAlignment == uiExpectedAlignment);

  T testOnStack = T();
  EZ_TEST(ezMemoryUtils::IsAligned(&testOnStack, uiExpectedAlignment));

  T* pTestBuffer = EZ_NEW_RAW_BUFFER(pAllocator, T, 32);
  ezArrayPtr<T> TestArray = EZ_NEW_ARRAY(pAllocator, T, 32);

  // default constructor should be called even if we declare as a pod type
  EZ_TEST(TestArray[0].x == 5.0f);
  EZ_TEST(TestArray[0].y == 6.0f);
  EZ_TEST(TestArray[0].z == 8.0f);

  EZ_TEST(ezMemoryUtils::IsAligned(pTestBuffer, uiExpectedAlignment));
  EZ_TEST(ezMemoryUtils::IsAligned(TestArray.GetPtr(), uiExpectedAlignment));

  size_t uiExpectedSize = sizeof(T) * 32;
  EZ_TEST(pAllocator->AllocatedSize(pTestBuffer) == uiExpectedSize);

  ezIAllocator::Stats stats;
  pAllocator->GetStats(stats);
  EZ_TEST(stats.m_uiAllocationSize == uiExpectedSize * 2);
  EZ_TEST(stats.m_uiNumLiveAllocations == 2);

  EZ_DELETE_ARRAY(pAllocator, TestArray);
  EZ_DELETE_RAW_BUFFER(pAllocator, pTestBuffer);

  pAllocator->GetStats(stats);
  EZ_TEST(stats.m_uiAllocationSize == 0);
  EZ_TEST(stats.m_uiNumLiveAllocations == 0);
}

EZ_CREATE_SIMPLE_TEST_GROUP(Memory);

EZ_CREATE_SIMPLE_TEST(Memory, Allocator)
{
  EZ_TEST_BLOCK(true, "Alignment")
  {
    TestAlignmentHelper<NonAlignedVector>(4);
    TestAlignmentHelper<AlignedVector>(16);
  }

  EZ_TEST_BLOCK(true, "Guards")
  {
    typedef ezAllocator<ezMemoryPolicies::ezHeapAllocation, ezMemoryPolicies::ezGuardedBoundsChecking,
      ezMemoryPolicies::ezSimpleTracking, ezMutex> GuardedAllocator;

    ezIAllocator* pAllocator = EZ_NEW(ezFoundation::GetBaseAllocator(), GuardedAllocator)("GuardedAllocator", NULL);

    char* szTestBuffer = EZ_NEW_RAW_BUFFER(pAllocator, char, 16);

    EZ_TEST(pAllocator->AllocatedSize(szTestBuffer) == 16);

    /*ezIAllocator::Stats stats;
    pAllocator->GetStats(stats);
    EZ_TEST(stats.m_uiAllocationSize == 16);*/

    strcpy(szTestBuffer, "TestTestTestTes");

    // buffer overflow
    //strcpy(szTestBuffer, "TestTestTestTest1");

    // buffer underflow
    //szTestBuffer[-1] = 'b';

    EZ_DELETE_RAW_BUFFER(pAllocator, szTestBuffer);

    EZ_DELETE(ezFoundation::GetBaseAllocator(), pAllocator);
  }

  EZ_TEST_BLOCK(true, "Tracking")
  {
    typedef ezAllocator<ezMemoryPolicies::ezHeapAllocation, ezMemoryPolicies::ezNoBoundsChecking,
      ezMemoryPolicies::ezStackTracking, ezMutex> TrackingAllocator;

    ezIAllocator* pAllocator = EZ_NEW(ezFoundation::GetBaseAllocator(), TrackingAllocator)("TrackingAllocator", NULL);

    // memory leak
    char* szTestBuffer = EZ_NEW_RAW_BUFFER(pAllocator, char, 16);

    static_cast<TrackingAllocator*>(pAllocator)->GetTracker().DumpMemoryLeaks();

    EZ_DELETE_RAW_BUFFER(pAllocator, szTestBuffer);

    EZ_DELETE(ezFoundation::GetBaseAllocator(), pAllocator);
  }

  EZ_TEST_BLOCK(true, "LargeBlockAllocator")
  {
    ezLargeBlockAllocator allocator("Test", ezFoundation::GetDefaultAllocator());

    ezDynamicArray<ezDataBlock<int> > blocks;
    blocks.Reserve(1000);

    for (ezUInt32 i = 0; i < 17; ++i)
    {
      ezDataBlock<int> block = allocator.AllocateBlock<int>();
      EZ_TEST(ezMemoryUtils::IsAligned(block.m_pData, 4096)); // test page alignment
      EZ_TEST_INT(block.m_uiCount, 0);

      blocks.PushBack(block);
    }

    ezIAllocator::Stats stats;
    allocator.GetStats(stats);

    EZ_TEST(stats.m_uiNumAllocations == 17);
    EZ_TEST(stats.m_uiNumDeallocations == 0);
    EZ_TEST(stats.m_uiNumLiveAllocations == 17);
    EZ_TEST(stats.m_uiAllocationSize == 17 * 4096);
    EZ_TEST(stats.m_uiUsedMemorySize == 32 * 4096);

    for (ezUInt32 i = 0; i < 2000; ++i)
    {
      ezUInt32 uiAction = rand() % 2;
      if (uiAction == 0)
      {
        blocks.PushBack(allocator.AllocateBlock<int>());
      }
      else
      {
        ezUInt32 uiIndex = rand() % blocks.GetCount();
        ezDataBlock<int> block = blocks[uiIndex];

        allocator.DeallocateBlock(block);

        blocks.RemoveAtSwap(uiIndex);
      }
    }

    for (ezUInt32 i = 0; i < blocks.GetCount(); ++i)
    {
      allocator.DeallocateBlock(blocks[i]);
    }

    allocator.GetStats(stats);

    EZ_TEST(stats.m_uiNumLiveAllocations == 0);
    EZ_TEST(stats.m_uiAllocationSize == 0);
    EZ_TEST(stats.m_uiUsedMemorySize <= 5 * 32 * 4096);
  }
}

