#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/CUDADataType.h>
#include <ATen/cuda/CUDASparse.h>
#include <ATen/cuda/CUDASparseDescriptors.h>
#include <ATen/native/LinearAlgebraUtils.h>
#include <ATen/native/cuda/MiscUtils.h>

#if AT_USE_CUSPARSE_GENERIC_API()

namespace at {
namespace cuda {
namespace sparse {

namespace {

// If a specific GPU model does not provide native support for a given data
// type, cuSparse routines return CUSPARSE_STATUS_ARCH_MISMATCH error
void check_supported_cuda_type(cudaDataType cuda_type) {
  if (cuda_type == CUDA_R_16F) {
    cudaDeviceProp* prop = at::cuda::getCurrentDeviceProperties();
    TORCH_CHECK(
        prop->major >= 5 && ((10 * prop->major + prop->minor) >= 53),
        "Sparse operations with CUDA tensors of Float16 type are not supported on GPUs with compute capability < 5.3 (current: ",
        prop->major,
        ".",
        prop->minor,
        ")");
  }
#if defined(CUDA_VERSION) && CUDA_VERSION >= 11000
  if (cuda_type == CUDA_R_16BF) {
    cudaDeviceProp* prop = at::cuda::getCurrentDeviceProperties();
    TORCH_CHECK(
        prop->major >= 8,
        "Sparse operations with CUDA tensors of BFloat16 type are not supported on GPUs with compute capability < 8.0 (current: ",
        prop->major,
        ".",
        prop->minor,
        ")");
  }
#endif
}

} // anonymous namespace

cusparseIndexType_t getCuSparseIndexType(const c10::ScalarType& scalar_type) {
  if (scalar_type == c10::ScalarType::Int) {
    return CUSPARSE_INDEX_32I;
  } else if (scalar_type == c10::ScalarType::Long) {
    return CUSPARSE_INDEX_64I;
  } else {
    TORCH_INTERNAL_ASSERT(
        false, "Cannot convert type ", scalar_type, " to cusparseIndexType.");
  }
}

CuSparseDnMatDescriptor::CuSparseDnMatDescriptor(const Tensor& input) {
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(input.layout() == kStrided);
  IntArrayRef input_strides = input.strides();
  IntArrayRef input_sizes = input.sizes();
  auto ndim = input.dim();
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(ndim == 2);
  auto rows = input_sizes[ndim - 2];
  auto cols = input_sizes[ndim - 1];

  bool is_column_major =
      at::native::is_blas_compatible_column_major_order(input);
  bool is_row_major = at::native::is_blas_compatible_row_major_order(input);
  TORCH_INTERNAL_ASSERT(
      is_column_major || is_row_major,
      "Expected either row or column major contiguous input.");

  auto leading_dimension =
      is_row_major ? input_strides[ndim - 2] : input_strides[ndim - 1];
  auto order = is_row_major ? CUSPARSE_ORDER_ROW : CUSPARSE_ORDER_COL;

  void* values_ptr = input.data_ptr();

  cudaDataType value_type = ScalarTypeToCudaDataType(input.scalar_type());
  check_supported_cuda_type(value_type);

  cusparseDnMatDescr_t raw_descriptor;
  TORCH_CUDASPARSE_CHECK(cusparseCreateDnMat(
      &raw_descriptor,
      rows,
      cols,
      leading_dimension,
      values_ptr,
      value_type,
      order));

  descriptor_.reset(raw_descriptor);
}

CuSparseDnVecDescriptor::CuSparseDnVecDescriptor(const Tensor& input) {
  // cuSPARSE doesn't support batched vectors
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(
      input.dim() == 1 || (input.dim() == 2 && input.size(-1) == 1));

  // cuSPARSE doesn't support non-contiguous vectors
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(input.is_contiguous());

  cudaDataType value_type = ScalarTypeToCudaDataType(input.scalar_type());
  check_supported_cuda_type(value_type);

  cusparseDnVecDescr_t raw_descriptor;
  TORCH_CUDASPARSE_CHECK(cusparseCreateDnVec(
      &raw_descriptor, input.numel(), input.data_ptr(), value_type));
  descriptor_.reset(raw_descriptor);
}

CuSparseSpMatCsrDescriptor::CuSparseSpMatCsrDescriptor(const Tensor& input) {
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(input.is_sparse_csr());
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(input.dim() == 2);

  IntArrayRef input_sizes = input.sizes();
  auto ndim = input.dim();
  auto rows = input_sizes[ndim - 2];
  auto cols = input_sizes[ndim - 1];
  auto nnz = input._nnz();

  auto crow_indices = input.crow_indices();
  auto col_indices = input.col_indices();
  auto values = input.values();

  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(crow_indices.is_contiguous());
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(col_indices.is_contiguous());
  TORCH_INTERNAL_ASSERT_DEBUG_ONLY(values.is_contiguous());

  cusparseIndexType_t index_type =
      getCuSparseIndexType(crow_indices.scalar_type());
  cudaDataType value_type = ScalarTypeToCudaDataType(input.scalar_type());
  check_supported_cuda_type(value_type);

  cusparseSpMatDescr_t raw_descriptor;
  TORCH_CUDASPARSE_CHECK(cusparseCreateCsr(
      &raw_descriptor, // output descriptor
      rows,
      cols,
      nnz,
      crow_indices
          .data_ptr(), // row offsets of the sparse matrix, size = rows + 1
      col_indices.data_ptr(), // column indices of the sparse matrix, size = nnz
      values.data_ptr(), // values of the sparse matrix, size = nnz
      index_type, // data type of row offsets index
      index_type, // data type of col indices
      CUSPARSE_INDEX_BASE_ZERO, // base index of row offset and col indes
      value_type // data type of values
      ));

  descriptor_.reset(raw_descriptor);
}

} // namespace sparse
} // namespace cuda
} // namespace at

#endif // AT_USE_CUSPARSE_GENERIC_API()
