import matrixMultiply from './examples/matrix_multiply.s?raw';
import pipelineFpCompareConvert from './examples/pipeline_fp_compare_convert.s?raw';
import pipelineFpDenseDouble from './examples/pipeline_fp_dense_double.s?raw';
import pipelineFpDenseSingle from './examples/pipeline_fp_dense_single.s?raw';
import binarySearch from './examples/binary_search.s?raw';
import bubbleSort from './examples/bubble_sort.s?raw';
import fibonacci from './examples/fibonacci.s?raw';
import gcd from './examples/gcd.s?raw';
import pipelineFpIntToFp from './examples/pipeline_fp_int_to_fp.s?raw';
import pipelineFpStoreData from './examples/pipeline_fp_store_data.s?raw';
import pipelineHazardCheck from './examples/pipeline_hazard_check.s?raw';
import pipelineIntHazards from './examples/pipeline_int_hazards.s?raw';
import primeCheck from './examples/prime_check.s?raw';
import quicksort from './examples/quicksort.s?raw';
import rv64imfdPipelineSmokeTest from './examples/rv64imfd_pipeline_smoke_test.s?raw';
import rv64imfdSmokeTest from './examples/rv64imfd_smoke_test.s?raw';
import stringOperations from './examples/string_operations.s?raw';

export const examples = [
  {
    title: '3×3 Matrix Multiplication',
    description: 'Multiplies two 3×3 integer matrices and prints the result.',
    source: matrixMultiply,
  },
  {
    title: 'FP Compare & Convert',
    description: 'Exercises floating-point comparisons and conversion to integer registers.',
    source: pipelineFpCompareConvert,
  },
  {
    title: 'Dense FP Pipeline · Double',
    description: 'Chains double-precision operations to demonstrate pipeline hazards and forwarding.',
    source: pipelineFpDenseDouble,
  },
  {
    title: 'Dense FP Pipeline · Single',
    description: 'Chains single-precision operations to demonstrate pipeline hazards and forwarding.',
    source: pipelineFpDenseSingle,
  },
  {
    title: 'Binary Search',
    description: 'Searches a sorted integer array for a target value.',
    source: binarySearch,
  },
  {
    title: 'Bubble Sort',
    description: 'Sorts an integer array with the bubble sort algorithm.',
    source: bubbleSort,
  },
  {
    title: 'Fibonacci Sequence',
    description: 'Generates Fibonacci terms and stores them in memory.',
    source: fibonacci,
  },
  {
    title: 'Greatest Common Divisor',
    description: 'Calculates the GCD of two integers using repeated division.',
    source: gcd,
  },
  {
    title: 'FP Integer Conversions',
    description: 'Exercises integer to floating-point conversions and register moves.',
    source: pipelineFpIntToFp,
  },
  {
    title: 'FP Store Data Hazards',
    description: 'Tests floating-point store dependencies after loads and arithmetic.',
    source: pipelineFpStoreData,
  },
  {
    title: 'Pipeline Hazard Check',
    description: 'Demonstrates integer dependencies, load-use stalls, and a taken branch.',
    source: pipelineHazardCheck,
  },
  {
    title: 'Integer Pipeline Hazards',
    description: 'Exercises integer RAW, load-use, and store-data dependencies.',
    source: pipelineIntHazards,
  },
  {
    title: 'Prime Checker',
    description: 'Checks whether an integer is prime using branches and arithmetic.',
    source: primeCheck,
  },
  {
    title: 'Quicksort',
    description: 'Sorts an integer array with a recursive quicksort implementation.',
    source: quicksort,
  },
  {
    title: 'RV64IMAFD Pipeline Smoke Test',
    description: 'Checks floating-point loads and arithmetic with spacing for a basic pipeline.',
    source: rv64imfdPipelineSmokeTest,
  },
  {
    title: 'RV64IMAFD Smoke Test',
    description: 'Runs a compact mix of integer, multiply/divide, and floating-point instructions.',
    source: rv64imfdSmokeTest,
  },
  {
    title: 'String Operations',
    description: 'Demonstrates string length, copying, and reversing.',
    source: stringOperations,
  },
] as const;
