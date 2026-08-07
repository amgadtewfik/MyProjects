'use client';

import { useEffect } from 'react';
import { useRouter } from 'next/navigation';
import { useAuth } from '@/components/AuthContext';

export default function HomePage() {
  const { user, isLoading } = useAuth();
  const router = useRouter();

  useEffect(() => {
    if (isLoading) return;
    if (user) {
      router.push('/dashboard');
    } else {
      router.push('/login');
    }
  }, [user, isLoading, router]);

  return <div>Loading...</div>;
}
