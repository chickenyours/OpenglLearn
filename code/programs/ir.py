import os
import shutil

def find_images_in_folder(folder_path):
    """递归查找所有图片文件"""
    image_extensions = ['.jpg', '.jpeg', '.png', '.bmp', '.gif']  # 可以根据需要添加其他扩展名
    image_files = []
    
    for root, dirs, files in os.walk(folder_path):
        for file in files:
            if any(file.lower().endswith(ext) for ext in image_extensions):
                image_files.append(os.path.join(root, file))
    
    return image_files

def copy_image(src_path, dest_path):
    """将图片从源路径复制到目标路径"""
    try:
        shutil.copy2(src_path, dest_path)  # copy2 保留元数据
        print(f"图片已复制: {src_path} -> {dest_path}")
    except Exception as e:
        print(f"复制图片 {src_path} 时出错: {e}")

def replace_images_in_folder(folder_path):
    """遍历文件夹中的所有图片，生成副本并替换原图片"""
    images = find_images_in_folder(folder_path)
    
    if not images:
        print("没有找到图片文件。")
        return
    
    print(f"找到{len(images)}张图片，正在处理...")
    
    # 遍历每张图片，进行替换操作
    for image_path in images:
        # 生成临时副本路径
        temp_path = image_path + ".temp"
        
        # 复制图片到临时文件
        copy_image(image_path, temp_path)
        
        # 生成备份路径
        backup_path = image_path + ".backup"
        
        # 将原图片重命名为备份
        try:
            os.rename(image_path, backup_path)
            print(f"原图片已重命名为备份: {image_path} -> {backup_path}")
        except Exception as e:
            print(f"重命名原图片时出错: {e}")
        
        # 将临时副本重命名为原图片
        try:
            os.rename(temp_path, image_path)
            print(f"临时副本已重命名为原图片: {temp_path} -> {image_path}")
        except Exception as e:
            print(f"重命名临时副本时出错: {e}")

def main():
    source_folder = input("请输入源文件夹路径: ")
    
    if not os.path.exists(source_folder):
        print(f"源文件夹不存在: {source_folder}")
        return
    
    # 开始遍历并替换文件夹中的图片
    replace_images_in_folder(source_folder)
    print("所有图片处理完成。")

if __name__ == '__main__':
    main()
