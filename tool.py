# import numpy as np

# def generate_gaussian_kernel(kernel_size, sigma):
#     # 计算半边长
#     half_size = kernel_size // 2
#     # 创建一维高斯模糊核
#     kernel = np.zeros(kernel_size)
#     # 计算高斯核的每个值
#     for i in range(-half_size, half_size + 1):
#         kernel[i + half_size] = (1 / (np.sqrt(2 * np.pi) * sigma)) * np.exp(- (i ** 2) / (2 * sigma ** 2))
    
#     # 归一化
#     kernel /= np.sum(kernel)
#     return kernel

# # 设置核的大小和标准差
# kernel_size = 5
# sigma = 1.0
# gaussian_kernel = generate_gaussian_kernel(kernel_size, sigma)

# # 打印结果
# print("高斯模糊核:", gaussian_kernel)

# 检查 HDR 图像的亮度范围
import cv2
import numpy as np

# 读取 HDR 图像
image = cv2.imread('C:/Users/16620/Desktop/projects/OpenglLearn/bin/images/studio_small_03_1k_output.hdr', cv2.IMREAD_ANYDEPTH)

# 检查图像是否成功加载
if image is None:
    print("无法加载 HDR 图像!")
else:
    # 获取图像的亮度值 (luminance)，使用 RGB 的加权平均值来计算亮度
    luminance = 0.2126 * image[..., 0] + 0.7152 * image[..., 1] + 0.0722 * image[..., 2]

    # 查找图像中的最大亮度
    max_luminance = np.max(luminance)
    print("最大亮度值:", max_luminance)

    # 检查最大亮度是否超过 1.0（显示器的典型最大亮度值）
    if max_luminance > 1.0:
        print("图像中存在超过显示器范围的亮度!")
    else:
        print("图像中没有超过显示器范围的亮度。")


# 手动提高 .hdr 图形的亮度,输出到新的文件
# import cv2
# import numpy as np

# def increase_hdr_brightness(input_file, output_file, brightness_factor):
#     # 读取 HDR 图像，使用 IMREAD_ANYDEPTH 读取浮点格式的图像
#     image = cv2.imread(input_file, cv2.IMREAD_ANYDEPTH)

#     # 检查图像是否成功加载
#     if image is None:
#         print("无法加载图像!")
#         return
    
#     # 增加图像亮度
#     image = image * brightness_factor

#     # 确保图像的亮度值在合理范围内，避免溢出
#     image = np.clip(image, 0.0, 1000.0)  # 限制最大值为 1000.0

#     # 保存修改后的图像
#     cv2.imwrite(output_file, image)

#     print(f"亮度已增加，输出图像保存在: {output_file}")


# if __name__ == "__main__":
#     input_file = "C:/Users/16620/Desktop/projects/OpenglLearn/bin/images/studio_small_03_1k.hdr"  # 输入文件路径
#     output_file = "C:/Users/16620/Desktop/projects/OpenglLearn/bin/images/studio_small_03_1k_output.hdr"  # 输出文件路径
#     brightness_factor = 10.0  # 输入倍数，这里是将亮度增加 10 倍
    
#     # 调用函数增加亮度
#     increase_hdr_brightness(input_file, output_file, brightness_factor)


