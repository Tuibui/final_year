from setuptools import find_packages, setup
import os
from glob import glob

package_name = 'keyence_glr_driver'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'launch'), glob('launch/*.launch.py')),
        (os.path.join('share', package_name, 'config'), glob('config/*.yaml')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='tuibui',
    maintainer_email='jedayhotmail@gmail.com',
    description='Keyence GL-R Modbus/TCP driver (GC-1000 / GC-Link) for ROS 2 Humble.',
    license='MIT',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'gc_modbus_driver = keyence_glr_driver.gc_modbus_driver:main',
            'mock_publisher = keyence_glr_driver.mock_publisher:main',
        ],
    },
)
